#include "OpAmpClipperDistortionProcessor.h"
#include "PotTaper.h"
#include "DualMono.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double vcc = 9.0;
    constexpr double vBias = 4.5; // the supply divider's midpoint: the virtual ground the circuit is referenced to

    // 1N270 germanium. Not the Centaur's 1N34A model: this pedal runs its diodes at a fraction of a milliamp (10K in
    // series, ~3 V swing), and ElectroSmash measures its hard clipping at 700-800 mVpp (+-0.35..0.4 V) with a quoted Vf of
    // 0.3-0.45 V. With N = 1.3, Is = 5 nA puts +-0.37 V at 0.3 mA (see the doc); 200 nA would clip at +-0.24 V.
    constexpr double geIs = 5.0e-9;
    constexpr double geNVt = 1.3 * 25.85e-3;

    // 1N4148 (same SPICE parameters as the Tube Screamer / BD-2 models).
    constexpr double siIs = 2.52e-9;
    constexpr double siNVt = 1.752 * 25.85e-3;

    // uA741: open-loop gain 200 000 (106 dB), gain-bandwidth 1 MHz (dominant pole ~5 Hz), output resistance ~75 ohm,
    // swing ~1.5 V short of each rail of a 9 V supply. No offset, no slew limit (see the doc).
    const NodalCircuit::OpAmpMacro lm741 { 2.0e5, 1.0e6, 75.0, 1.5, 7.5, 0.0, 0.0 };

    constexpr double downstreamLoad = 1.0e6;    // assumed input impedance of what follows
}

const OpAmpClipperDistortionProcessor::ModelSpec& OpAmpClipperDistortionProcessor::specFor (Model model) noexcept
{
    // MXR Distortion+: the schematic from the GitHub schematic collection, cross-checked with ElectroSmash/Wampler.
    // DOD 250: General Guitar Gadgets' schematic (original 741 version): 22K/22K + 10 uF bias, 470K to the (+) pin,
    // 25 pF across the feedback, 500K reverse-log Gain, 4.7 uF coupling, 1N4148 pair, 100K Level.
    static const ModelSpec distortionPlus { "Distortion+-Style Distortion", "dp", "Distortion", "Output", 0xffe8c020,
                                            1.0e6, 1.0e-6, 10.0e-9, 10.0e3, 1.0e6, 1.0e6, 0.0, 47.0e-9, 4.7e3, 1.0e6,
                                            1.0e-6, 10.0e3, 1.0e-9, geIs, geNVt, 10.0e3, true };
    static const ModelSpec dod250 { "DOD 250-Style Overdrive", "dod250", "Gain", "Level", 0xfff0d830,
                                    22.0e3, 10.0e-6, 10.0e-9, 10.0e3, 470.0e3, 1.0e6, 25.0e-12, 47.0e-9, 4.7e3, 500.0e3,
                                    4.7e-6, 10.0e3, 1.0e-9, siIs, siNVt, 100.0e3, false };
    return model == Model::dod250 ? dod250 : distortionPlus;
}

OpAmpClipperDistortionProcessor::OpAmpClipperDistortionProcessor (Model model)
    : spec (specFor (model))
{
    const juce::String prefix (spec.idPrefix);
    auto distortionParam = std::make_unique<juce::AudioParameterFloat> (
        prefix + "_distortion", spec.distortionLabel, juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto outputParam = std::make_unique<juce::AudioParameterFloat> (
        prefix + "_output", spec.outputLabel, juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);

    distortion = distortionParam.get();
    output = outputParam.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        spec.idPrefix, spec.displayName, "|", std::move (distortionParam));
    group->addChild (std::move (outputParam));
    parameters = std::move (group);
}

void OpAmpClipperDistortionProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;
    auto& c = ch.c;

    const auto v9 = c.addNode(), nb = c.addNode(), in = c.addNode();
    const auto nA = c.addNode(), plus = c.addNode(), minus = c.addNode(), n3 = c.addNode(), n4 = c.addNode();
    const auto opOut = c.addNode(), nc = c.addNode(), nd = c.addNode(), nw = c.addNode();

    c.addSource (v9, vcc);
    ch.srcIn = c.addSource (in, 0.0);

    // Bias: a divider of two equal resistors from the 9 V supply with a decoupling cap (the pedal's 4.5 V reference).
    c.addResistor (v9, nb, spec.biasLegOhms);
    c.addResistor (nb, gnd, spec.biasLegOhms);
    c.addCapacitor (nb, gnd, spec.biasCapFarads);

    // Input: C2 -> R1 -> (+) pin, the pin returned to the reference through the bias resistor. (The 1 nF from the jack to
    // ground in the schematics loads the pickup, not this circuit: an ideal source in front of it makes it a no-op.)
    c.addCapacitor (in, nA, spec.inputCapFarads);
    c.addResistor (nA, plus, spec.inputSeriesOhms);
    c.addResistor (plus, nb, spec.plusBiasOhms);

    // Non-inverting stage: feedback resistor (and a small cap on the 250), (-) leg = cap + fixed resistor + the rheostat.
    c.addOpAmpMacro (plus, minus, opOut, lm741);
    c.addResistor (minus, opOut, spec.feedbackOhms);
    if (spec.feedbackCapFarads > 0.0)
        c.addCapacitor (minus, opOut, spec.feedbackCapFarads);
    c.addCapacitor (minus, n3, spec.legCapFarads);
    c.addResistor (n3, n4, spec.legOhms);
    ch.rDistortion = c.addResistor (n4, gnd, 1.0e3);

    // Clipping and output: the coupling cap blocks the 4.5 V -> 10K -> diode pair to ground with a small cap across; the
    // Output pot sits across the diodes (its top is the diode node) and its wiper is the pedal's output.
    c.addCapacitor (opOut, nc, spec.couplingCapFarads);
    c.addResistor (nc, nd, spec.clipSeriesOhms);
    c.addDiode (nd, gnd, spec.diodeIs, spec.diodeNVt, 4.0e-9);
    c.addDiode (gnd, nd, spec.diodeIs, spec.diodeNVt, 4.0e-9);
    c.addCapacitor (nd, gnd, spec.clipCapFarads);
    ch.rOutTop = c.addResistor (nd, nw, 1.0);
    ch.rOutBottom = c.addResistor (nw, gnd, 1.0);
    c.addResistor (nw, gnd, downstreamLoad);

    ch.nOpOut = opOut;
    ch.nDiodes = nd;
    ch.nMinus = minus;
    ch.nOut = nw;

    for (auto n : { nb, nA, plus, minus, opOut })
        c.setInitialGuess (n, vBias);
}

void OpAmpClipperDistortionProcessor::updatePots (double distortionKnob, double outputKnob)
{
    // Distortion: a rheostat in the (-) leg, so LESS resistance = MORE gain. Wired reverse-audio (the 250's schematic says
    // "reverse log"), R = pot (1-knob)^2 --
    // the taper the gain range needs (with a linear one everything above a gain of 3 is crammed into the last 5% of
    // the knob; see the doc).
    const double rDist = juce::jmax (1.0, spec.distortionPotOhms * pots::audio (1.0 - distortionKnob));

    // Output: audio taper, wiper-to-ground segment.
    const double oBottom = juce::jmax (1.0, spec.outputPotOhms * pots::audio (outputKnob));
    const double oTop = juce::jmax (1.0, spec.outputPotOhms - oBottom);

    for (auto& ch : channels)
    {
        ch.c.setResistance (ch.rDistortion, rDist);
        ch.c.setResistance (ch.rOutTop, oTop);
        ch.c.setResistance (ch.rOutBottom, oBottom);
    }
}

void OpAmpClipperDistortionProcessor::prepare (double newSampleRate, int, int)
{
    // Same-rate re-prepare is a no-op (see the Centaur/DS-1 processors for why).
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
    }

    smoothedDistortion.reset (newSampleRate, 0.02);
    smoothedDistortion.setCurrentAndTargetValue (distortion->get());
    smoothedOutput.reset (newSampleRate, 0.02);
    smoothedOutput.setCurrentAndTargetValue (output->get());

    updatePots (distortion->get(), output->get());

    dcOk = true;
    for (auto& ch : channels)
        dcOk = ch.c.prepare (newSampleRate) && dcOk;

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void OpAmpClipperDistortionProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedDistortion.setTargetValue (distortion->get());
    smoothedOutput.setTargetValue (output->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float d = smoothedDistortion.getNextValue();
        const float o = smoothedOutput.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots (d, o);
        }

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            ch.c.setSource (ch.srcIn, (double) data[i]);
            const bool ok = ch.c.solveSample();
            data[i] = (float) ch.c.voltage (ch.nOut);

            if (chIdx == 0)
            {
                ++sampleCount;
                if (! ok)
                    ++failureCount;
            }
        }
    }

    shortcut.end (buffer);
}

void OpAmpClipperDistortionProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // The Distortion+ is a distortion (flat-topped wave, Assets/Icons/distortion.svg), the 250 an overdrive (rounded wave,
    // overdrive.svg) -- the names the pedals were sold under. See docs/icons/AGENT-icon-notes.md.
    static const std::unique_ptr<juce::Drawable> distortionSvg = icon::loadSvg (IconData::distortion_svg, IconData::distortion_svgSize);
    static const std::unique_ptr<juce::Drawable> overdriveSvg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, (spec.distortionGlyph ? distortionSvg : overdriveSvg).get());
}

} // namespace openguitarmultifx
