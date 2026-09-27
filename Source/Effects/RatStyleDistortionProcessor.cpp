#include "RatStyleDistortionProcessor.h"
#include "PotTaper.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double vBias = 4.5; // R 100K / 100K + 1 uF (only feeds the (+) pin's 1M: taken as ideal)

    // 1N914-class silicon (same SPICE parameters as the Tube Screamer / BD-2 models).
    constexpr double siIs = 2.52e-9;
    constexpr double siNVt = 1.752 * 25.85e-3;

    // LM308 with the 30 pF compensation cap: open-loop gain 300 000, unity-gain ~1 MHz, output resistance ~100 ohm,
    // swing ~1.5 V short of each rail. Its 0.3 V/us slew rate is NOT modelled (see the doc).
    const NodalCircuit::OpAmpMacro lm308 { 3.0e5, 1.0e6, 100.0, 1.5, 7.5, 0.0, 0.0 };

    // Red LED (the Guv'nor's / Nobels': ~1.75 V at 0.5 mA).
    constexpr double ledIs = 1.3e-19;
    constexpr double ledNVt = 1.9 * 25.85e-3;

    // OP07 (the Turbo RAT of the Effects Layouts bill of materials): 106 dB, 0.6 MHz gain-bandwidth, ~75 ohm out. A bipolar
    // output stage stops ~2 V short of each rail of a 9 V supply. Its 0.3 V/us slew rate is not modelled, like the LM308's.
    const NodalCircuit::OpAmpMacro op07 { 4.0e5, 0.6e6, 75.0, 2.0, 7.0, 0.0, 0.0 };

    constexpr double filterPotMax = 100.0e3;     // "100k log" rheostat
    constexpr double volumePotMax = 100.0e3;     // "100k log"
    constexpr double followerDrop = -2.6;        // 2N5458 source follower with its gate at 0 V and 10K in the source: Vgs ~ -2.6 V
    constexpr double downstreamLoad = 1.0e6;
}

const RatStyleDistortionProcessor::Spec& RatStyleDistortionProcessor::specFor (Model model) noexcept
{
    // Original: Rev P of the reverse-engineered schematic (150K Distortion, 360 ohm, 1K6 -- three of the other sources say
    // 100K / 560 / 1K5, see the doc). RAT 2 and Turbo RAT: the Effects Layouts bill of materials, whose columns differ in
    // R4/R5 (2M2), C13 (10 uF) and, for the Turbo, the LEDs and the OP07.
    static const Spec original { "RAT-Style Distortion", 1.0e6, 360.0, 150.0e3, 1.6e3, 1.0e-6, false, lm308 };
    static const Spec rat2 { "RAT 2-Style Distortion", 2.2e6, 560.0, 100.0e3, 1.5e3, 10.0e-6, false, lm308 };
    static const Spec turbo { "Turbo RAT-Style Distortion", 2.2e6, 560.0, 100.0e3, 1.5e3, 10.0e-6, true, op07 };

    switch (model)
    {
        case Model::rat2:  return rat2;
        case Model::turbo: return turbo;
        case Model::original: break;
    }
    return original;
}

RatStyleDistortionProcessor::RatStyleDistortionProcessor (Model model) : spec (specFor (model))
{
    auto make = [] (const char* id, const char* name)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    };

    auto distortionParam = make ("rat_distortion", "Distortion");
    auto filterParam = make ("rat_filter", "Filter");
    auto volumeParam = make ("rat_volume", "Volume");

    distortion = distortionParam.get();
    filter = filterParam.get();
    volume = volumeParam.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "rat", spec.displayName, "|", std::move (distortionParam));
    group->addChild (std::move (filterParam));
    group->addChild (std::move (volumeParam));
    parameters = std::move (group);
}

void RatStyleDistortionProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;
    auto& c = ch.c;

    const auto nb = c.addNode(), in = c.addNode(), nA = c.addNode(), plus = c.addNode(), minus = c.addNode();
    const auto n1 = c.addNode(), n2 = c.addNode(), opOut = c.addNode();
    const auto nR = c.addNode(), nD = c.addNode(), nF1 = c.addNode(), nF = c.addNode(), nG = c.addNode(), nS = c.addNode();
    const auto nL = c.addNode(), nW = c.addNode();
    c.addSource (nb, vBias);
    ch.srcIn = c.addSource (in, 0.0);

    // Input: 22 nF -> node A (1M to the bias) -> 1K -> (+) pin, 1 nF from the pin to ground
    c.addCapacitor (in, nA, 22.0e-9);
    c.addResistor (nA, nb, spec.inputBiasOhms);
    c.addResistor (nA, plus, 1.0e3);
    c.addCapacitor (plus, gnd, 1.0e-9);

    // The LM308 with its gain network: (-) leg = 360 ohm + 4.7 uF and 47 ohm + 2.2 uF to ground (two shelves: the gain rises
    // in two steps), feedback = the Distortion rheostat || 100 pF
    c.addOpAmpMacro (plus, minus, opOut, spec.opAmp);
    c.addResistor (minus, n1, spec.resistorLegA);
    c.addCapacitor (n1, gnd, 4.7e-6);
    c.addResistor (minus, n2, 47.0);
    c.addCapacitor (n2, gnd, 2.2e-6);
    c.addCapacitor (minus, opOut, 100.0e-12);
    ch.rGain = c.addResistor (minus, opOut, 1.0e3);

    // 1K + 4.7 uF into the two clipping diodes to ground (silicon; red LEDs on the Turbo)
    const double clipIs = spec.ledClipping ? ledIs : siIs, clipNVt = spec.ledClipping ? ledNVt : siNVt;
    c.addResistor (opOut, nR, 1.0e3);
    c.addCapacitor (nR, nD, 4.7e-6);
    c.addDiode (nD, gnd, clipIs, clipNVt, 4.0e-9);
    c.addDiode (gnd, nD, clipIs, clipNVt, 4.0e-9);

    // Filter: the rheostat + 1K6 into a 3.3 nF low-pass, then 22 nF to the follower's gate (1M to ground)
    ch.rFilter = c.addResistor (nD, nF1, 1.0e3);
    c.addResistor (nF1, nF, spec.filterFixedOhms);
    c.addCapacitor (nF, gnd, 3.3e-9);
    c.addCapacitor (nF, nG, 22.0e-9);
    c.addResistor (nG, gnd, 1.0e6);

    // 2N5458 source follower (an ideal follower), 1 uF, 10K, then the Volume pot
    c.addFollower (nG, nS, followerDrop);
    c.addCapacitor (nS, nL, spec.outputCap);
    c.addResistor (nL, gnd, 10.0e3);
    ch.rLevelTop = c.addResistor (nL, nW, 1.0e3);
    ch.rLevelBottom = c.addResistor (nW, gnd, 1.0e3);
    c.addResistor (nW, gnd, downstreamLoad);

    ch.nOpOut = opOut;
    ch.nDiodes = nD;
    ch.nFilter = nF;
    ch.nOut = nW;

    for (auto n : { nA, plus, minus, opOut })
        c.setInitialGuess (n, vBias);
    c.setInitialGuess (nR, vBias);
    c.setInitialGuess (nS, -followerDrop);
    c.setInitialGuess (nL, 0.0);
}

void RatStyleDistortionProcessor::updatePots (double distortionKnob, double filterKnob, double volumeKnob)
{
    // Distortion: a log pot as a rheostat in the feedback: more resistance = more gain (pots::audio).
    const double rGain = juce::jmax (1.0, spec.distortionPotOhms * pots::audio (distortionKnob));

    // Filter: 100K log rheostat, more resistance = darker (475 Hz .. 30 kHz with the 1K6 and 3.3 nF).
    const double rFilter = juce::jmax (1.0, filterPotMax * pots::audio (filterKnob));

    // Volume: 100K log, wiper-to-ground segment.
    const double lBottom = juce::jmax (1.0, volumePotMax * pots::audio (volumeKnob));
    const double lTop = juce::jmax (1.0, volumePotMax - lBottom);

    for (auto& ch : channels)
    {
        ch.c.setResistance (ch.rGain, rGain);
        ch.c.setResistance (ch.rFilter, rFilter);
        ch.c.setResistance (ch.rLevelTop, lTop);
        ch.c.setResistance (ch.rLevelBottom, lBottom);
    }
}

void RatStyleDistortionProcessor::prepare (double newSampleRate, int, int)
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
    smoothedFilter.reset (newSampleRate, 0.02);
    smoothedFilter.setCurrentAndTargetValue (filter->get());
    smoothedVolume.reset (newSampleRate, 0.02);
    smoothedVolume.setCurrentAndTargetValue (volume->get());

    updatePots (distortion->get(), filter->get(), volume->get());

    dcOk = true;
    for (auto& ch : channels)
        dcOk = ch.c.prepare (newSampleRate) && dcOk;

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void RatStyleDistortionProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedDistortion.setTargetValue (distortion->get());
    smoothedFilter.setTargetValue (filter->get());
    smoothedVolume.setTargetValue (volume->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float d = smoothedDistortion.getNextValue();
        const float f = smoothedFilter.getNextValue();
        const float v = smoothedVolume.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots (d, f, v);
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

void RatStyleDistortionProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // Distortion glyph (Assets/Icons/distortion.svg): the overdrive wave with flat, hard-clipped tops and steeper sides -- between the
    // overdrive's rounded one and the (future) fuzz's square one. See docs/icons/AGENT-icon-notes.md.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::distortion_svg, IconData::distortion_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
