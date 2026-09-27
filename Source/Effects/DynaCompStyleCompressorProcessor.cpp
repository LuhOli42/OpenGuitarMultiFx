#include "DynaCompStyleCompressorProcessor.h"
#include "PotTaper.h"
#include "IconKit.h"

#include <IconData.h>

#include <cmath>

namespace openguitarmultifx
{

namespace
{
    constexpr double vcc = 9.0;
    constexpr double vt = 25.85e-3;
    constexpr double pin5Volts = 1.2;        // the CA3080's amplifier-bias pin sits two diode drops above V- (ground)
    constexpr double otaInputOhms = 1.0e6;   // "around 1M" each (ElectroSmash, from the datasheet's Zin vs Iabc)
    constexpr double q5Drop = 0.57;          // the bias-current follower's base-emitter drop at tens of uA
    constexpr double sustainPotMax = 500.0e3;
    constexpr double levelPotMax = 50.0e3;
    constexpr double downstreamLoad = 1.0e6;
}

const DynaCompStyleCompressorProcessor::Spec& DynaCompStyleCompressorProcessor::specFor (Model model) noexcept
{
    // Dyna Comp: ElectroSmash's schematic (R2 1M, R5 470K, C9 50 nF, 2N3904, VR = 9 V * 22K / 78K). Ross: Aion FX's table for the
    // Ross (R19 56K / R20 27K -> 2.93 V; R2 || R3 470K / 470K; the OTA feed R8 + R7 = 220K + 220K; C13 100 nF; C1 150 pF; 2N5088).
    static const Spec dyna { "Dyna Comp-Style Compressor", 2.54, 1.0e6, 0.0, 470.0e3, 50.0e-9, 200.0, false };
    static const Spec ross { "Ross-Style Compressor", 2.93, 235.0e3, 150.0e-12, 440.0e3, 100.0e-9, 500.0, true };
    return model == Model::ross ? ross : dyna;
}

DynaCompStyleCompressorProcessor::DynaCompStyleCompressorProcessor (Model model) : spec (specFor (model))
{
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto sustainParam = make ("dynacomp_sustain", "Sustain", 0.5f);
    auto levelParam = make ("dynacomp_level", "Level", 0.5f);
    sustain = sustainParam.get();
    level = levelParam.get();
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("dynacomp", spec.displayName, "|", std::move (sustainParam), std::move (levelParam));
}

void DynaCompStyleCompressorProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;
    const NodalCircuit::BjtParams npn { 1.0e-14, vt, spec.beta, 4.0 };

    // ================================================================ block A: the input follower and the OTA's two input networks
    {
        auto& c = ch.a;
        const auto vr = c.addNode(), v9 = c.addNode(), in = c.addNode(), n1 = c.addNode(), b1 = c.addNode(), e1 = c.addNode();
        const auto n2 = c.addNode(), w = c.addNode(), t = c.addNode(), s = c.addNode();
        c.addSource (vr, spec.vRef);
        c.addSource (v9, vcc);
        ch.srcIn = c.addSource (in, 0.0);

        // C1 (10 nF) and R1 (10K) into Q1's base (biased to VR); Q1 an emitter follower (R3 10K), C2 1 uF into the OTA's inputs
        c.addCapacitor (in, n1, 10.0e-9);
        if (spec.inputShuntFarads > 0.0)
            c.addCapacitor (n1, gnd, spec.inputShuntFarads);
        c.addResistor (n1, b1, 10.0e3);
        c.addResistor (b1, vr, spec.inputBiasOhms);
        c.addFollower (b1, e1, 0.6);
        c.addResistor (e1, gnd, 10.0e3);
        c.addCapacitor (e1, n2, 1.0e-6);
        c.addResistor (n2, gnd, 1.0e6); // R4

        // The OTA's (-) input is the signal node n2; its (+) input T is fed through the 2K trimmer (mid position: 1K + 1K, its wiper W
        // biased from +9 V by R5) and then a low-pass: R6 1M to ground, R7 15K || C4 10 nF into C3 1 uF to ground. The OTA amplifies
        // the difference between the two: ~1 dB at 300 Hz, ~4 dB at 10 kHz (a treble emphasis).
        c.addResistor (n2, w, 1.0e3);
        c.addResistor (w, t, 1.0e3);
        c.addResistor (w, v9, spec.otaFeedOhms);
        c.addResistor (t, gnd, 1.0e6); // R6
        c.addResistor (n2, gnd, otaInputOhms);
        c.addResistor (t, gnd, otaInputOhms);
        c.addResistor (t, s, 15.0e3);
        c.addCapacitor (t, s, 10.0e-9);
        c.addCapacitor (s, gnd, 1.0e-6);

        ch.nPlus = t;
        ch.nMinus = n2;
        c.setInitialGuess (b1, spec.vRef);
        c.setInitialGuess (e1, spec.vRef - 0.6);
        for (auto n : { w, t, s, n2 })
            c.setInitialGuess (n, 3.1);
    }

    // ================================================================ block B: the OTA's load, the phase splitter, the rectifiers, the bias-current follower, the output
    {
        auto& c = ch.b;
        const auto vr = c.addNode(), v9 = c.addNode(), p5 = c.addNode();
        const auto o = c.addNode(), c2 = c.addNode(), e2 = c.addNode(), d1 = c.addNode(), d2 = c.addNode(), env = c.addNode(), e5 = c.addNode();
        const auto out9 = c.addNode(), top = c.addNode(), wiper = c.addNode();
        c.addSource (vr, spec.vRef);
        c.addSource (v9, vcc);
        c.addSource (p5, pin5Volts);

        // The OTA's output current (set every sample) into Q2's base node: R8 150K and C5 1 nF to VR (the treble roll-off)
        ch.hOta = c.addCurrentSource (o, 0.0);
        c.addResistor (o, vr, 150.0e3);
        c.addCapacitor (o, vr, 1.0e-9);

        // Q2, the phase splitter: R9 10K collector, R10 10K emitter
        c.addBjt (c2, o, e2, false, npn);
        c.addResistor (v9, c2, 10.0e3);
        c.addResistor (e2, gnd, 10.0e3);

        // The two rectifiers: C6 / C7 10 nF into the bases of Q3 / Q4 (R11 / R12 1M to ground, D1 / D2 clamping negative swings); their collectors
        // join at the envelope node: R13 150K to +9 V and C8 10 uF to ground
        c.addCapacitor (c2, d1, 10.0e-9);
        c.addResistor (d1, gnd, 1.0e6);
        c.addDiode (gnd, d1, 1.0e-14, 1.752 * vt);
        c.addBjt (env, d1, gnd, false, npn);
        c.addCapacitor (e2, d2, 10.0e-9);
        c.addResistor (d2, gnd, 1.0e6);
        c.addDiode (gnd, d2, 1.0e-14, 1.752 * vt);
        c.addBjt (env, d2, gnd, false, npn);
        c.addResistor (v9, env, 150.0e3);
        c.addCapacitor (env, gnd, 10.0e-6);

        // Q5, an emitter follower from the envelope into the OTA's bias pin through 27K and the Sustain rheostat
        c.addFollower (env, e5, q5Drop);
        ch.rSustain = c.addResistor (e5, p5, 100.0e3);

        // Output: C9 into R14 10K and the Level pot, from Q2's emitter
        c.addCapacitor (e2, out9, spec.outputCoupling);
        c.addResistor (out9, top, 10.0e3);
        ch.rLevelTop = c.addResistor (top, wiper, 1.0e3);
        ch.rLevelBottom = c.addResistor (wiper, gnd, 1.0e3);
        c.addResistor (wiper, gnd, downstreamLoad);

        ch.nE5 = e5;
        ch.nEnv = env;
        ch.nE2 = e2;
        ch.nOut = wiper;
        c.setInitialGuess (o, spec.vRef);
        c.setInitialGuess (c2, 7.0);
        c.setInitialGuess (e2, spec.vRef - 0.6);
        c.setInitialGuess (env, 8.3);
        c.setInitialGuess (e5, 7.7);
        c.setInitialGuess (d1, 0.0);
        c.setInitialGuess (d2, 0.0);
        c.setInitialGuess (out9, 0.0);
        c.setInitialGuess (top, 0.0);
        c.setInitialGuess (wiper, 0.0);
    }
}

void DynaCompStyleCompressorProcessor::updatePots (double sustainKnob, double levelKnob)
{
    // Sustain: 500K rheostat in series with R16 27K; clockwise = less resistance = more bias current = more gain and more compression.
    const double fraction = spec.sustainReverseLog ? 1.0 - pots::reverseAudio (sustainKnob) : 1.0 - sustainKnob;
    biasChainOhms = 27.0e3 + juce::jmax (1.0, sustainPotMax * fraction);

    // Level: 50K, wiper to ground segment (linear "B").
    const double lBottom = juce::jmax (1.0, levelPotMax * levelKnob);
    const double lTop = juce::jmax (1.0, levelPotMax - lBottom);

    for (auto& ch : channels)
    {
        ch.b.setResistance (ch.rSustain, biasChainOhms);
        ch.b.setResistance (ch.rLevelTop, lTop);
        ch.b.setResistance (ch.rLevelBottom, lBottom);
    }
}

void DynaCompStyleCompressorProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
    }

    smoothedSustain.reset (newSampleRate, 0.02);
    smoothedSustain.setCurrentAndTargetValue (sustain->get());
    smoothedLevel.reset (newSampleRate, 0.02);
    smoothedLevel.setCurrentAndTargetValue (level->get());
    updatePots (sustain->get(), level->get());

    dcOk = true;
    for (auto& ch : channels)
    {
        dcOk = ch.a.prepare (newSampleRate) && dcOk;
        dcOk = ch.b.prepare (newSampleRate) && dcOk; // at DC the OTA's differential input is ~0: its current is 0 whatever the bias
        ch.iabc = juce::jmax (0.0, (ch.b.voltage (ch.nE5) - pin5Volts) / biasChainOhms);
    }

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void DynaCompStyleCompressorProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedSustain.setTargetValue (sustain->get());
    smoothedLevel.setTargetValue (level->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float s = smoothedSustain.getNextValue();
        const float l = smoothedLevel.getNextValue();
        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots (s, l);
        }

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            ch.a.setSource (ch.srcIn, (double) data[i]);
            bool ok = ch.a.solveSample();

            // The CA3080: Iout = Iabc * tanh (Vd / 2 Vt), Iabc from the previous sample's envelope follower
            const double vd = ch.a.voltage (ch.nPlus) - ch.a.voltage (ch.nMinus);
            ch.b.setCurrentSource (ch.hOta, ch.iabc * std::tanh (vd / (2.0 * vt)));
            ok = ch.b.solveSample() && ok;
            ch.iabc = juce::jmax (0.0, (ch.b.voltage (ch.nE5) - pin5Volts) / biasChainOhms);

            data[i] = (float) ch.b.voltage (ch.nOut);

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

void DynaCompStyleCompressorProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // The sheet's Compressor glyph (Dinamica): the pulse trace.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::pulse_svg, IconData::pulse_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
