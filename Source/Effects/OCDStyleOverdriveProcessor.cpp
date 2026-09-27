#include "OCDStyleOverdriveProcessor.h"
#include "PotTaper.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double vBias = 4.5; // R12/R13 10K/10K + 47 uF ("VB"): taken as ideal

    // Red LED (the Guv'nor's / Nobels': ~1.75 V at 0.5 mA).
    constexpr double ledIs = 1.3e-19;
    constexpr double ledNVt = 1.9 * 25.85e-3;

    // The 2N7000's body diode (source to drain). The datasheet's only spec is VSD <= 1.2 V at ISD = 200 mA -- nowhere
    // near the ~0.1-1 mA this circuit actually runs it at. Extrapolated down with the diode equation at N = 1.4
    // (Is = 200 mA / exp(1.2 / nVt)): ~1.0 V at 1 mA, ~0.9 V at 0.1 mA. The earlier estimate (Is 2 pA: ~0.65-0.73 V,
    // 2026-09-21) clipped so early that the Clipping switch's MOSFET setting sounded the same regardless of
    // Drive/level, a user-reported bug (docs/circuits/HarshnessDiagnosis.md, "does the Clipping switch change the
    // sound?"); this Is is the same 1.4 ideality factor, recalibrated to the datasheet's own operating point instead
    // of a small-signal-diode guess.
    constexpr double bodyNVt = 1.4 * 25.85e-3;
    const double bodyIs = 0.2 / std::exp (1.2 / bodyNVt);

    // TL082: 106 dB, 3 MHz gain-bandwidth, ~100 ohm out, swing ~1.5 V short of each rail of a 9 V supply.
    const NodalCircuit::OpAmpMacro tl082 { 2.0e5, 3.0e6, 100.0, 1.5, 7.5, 0.0, 0.0 };

    constexpr double drivePotMax = 1.0e6;   // "1MA" (audio taper)
    constexpr double tonePotMax = 10.0e3;   // "10kB"
    constexpr double levelPotMax = 100.0e3; // "100kB"
    constexpr double downstreamLoad = 1.0e6;
    constexpr double switchClosedOhms = 1.0;
    constexpr double switchOpenOhms = 1.0e9;
}

OCDStyleOverdriveProcessor::OCDStyleOverdriveProcessor()
{
    auto make = [] (const char* id, const char* name)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    };
    auto makeSwitch = [] (const char* id, const char* name, float def, const char* off, const char* on)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            id, name, juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([off, on] (float v, int) { return juce::String (v < 0.5f ? off : on); }));
    };

    auto driveParam = make ("ocd_drive", "Drive");
    auto toneParam = make ("ocd_tone", "Tone");
    auto levelParam = make ("ocd_volume", "Volume");
    auto clipParam = makeSwitch ("ocd_clipping", "Clipping", 0.0f, "MOSFET", "LED");
    auto peakParam = makeSwitch ("ocd_peak", "HP/LP", 1.0f, "Low peak", "High peak");

    drive = driveParam.get();
    tone = toneParam.get();
    level = levelParam.get();
    clipping = clipParam.get();
    peak = peakParam.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "ocd", "OCD-Style Overdrive", "|", std::move (driveParam));
    group->addChild (std::move (toneParam));
    group->addChild (std::move (levelParam));
    group->addChild (std::move (clipParam));
    group->addChild (std::move (peakParam));
    parameters = std::move (group);
}

void OCDStyleOverdriveProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ block A: stage 1 and the clipper
    {
        auto& c = ch.a;
        const auto nb = c.addNode(), in = c.addNode(), nA = c.addNode(), plus = c.addNode(), minus = c.addNode();
        const auto nLeg = c.addNode(), o1 = c.addNode(), nF = c.addNode(), x = c.addNode(), nR = c.addNode();
        c.addSource (nb, vBias);
        ch.srcIn = c.addSource (in, 0.0);

        // 22 nF -> 10K -> (+) (470K to the bias). (-) leg 2K2 + 68 nF to ground; feedback 220 pF, and 18K + the Drive rheostat.
        c.addCapacitor (in, nA, 22.0e-9);
        c.addResistor (nA, plus, 10.0e3);
        c.addResistor (plus, nb, 470.0e3);
        c.addOpAmpMacro (plus, minus, o1, tl082);
        c.addResistor (minus, nLeg, 2.2e3);
        c.addCapacitor (nLeg, gnd, 68.0e-9);
        c.addCapacitor (minus, o1, 220.0e-12);
        c.addResistor (o1, nF, 18.0e3);
        ch.rDrive = c.addResistor (nF, minus, 1.0e3);

        // 10K into the clipper node, 1 nF to the bias
        c.addResistor (o1, x, 10.0e3);
        c.addCapacitor (x, nb, 1.0e-9);

        // Two red LEDs across the node, to the bias
        c.addDiode (x, nb, ledIs, ledNVt, 30.0e-9);
        c.addDiode (nb, x, ledIs, ledNVt, 30.0e-9);

        // The 2N7000s (gate and drain on one net each) through the Clipping switch to the bias:
        //   Q1: drain + gate = clipper node, source = the switch node;  Q2: drain + gate = the switch node, source = the node.
        // Each one's channel conducts when its gate is 2 V above its source, its body diode (source to drain) at ~0.65 V. The
        // body diodes clip first and hold the node inside +-0.7 V, so the channels never turn on and are not in the netlist
        // (a test checks the node stays under the threshold; docs/circuits/OCDStyleOverdrive.md). The Ge diodes of the other
        // versions are the 1.4 / Custom Shop options and are not fitted in 1.7.
        ch.rClipSwitch = c.addResistor (nR, nb, switchClosedOhms);
        c.addDiode (nR, x, bodyIs, bodyNVt, 4.0e-9);
        c.addDiode (x, nR, bodyIs, bodyNVt, 4.0e-9);

        ch.nOp1 = o1;
        ch.nClip = x;
        for (auto n : { nA, plus, minus, o1, nF, x, nR })
            c.setInitialGuess (n, vBias);
        c.setInitialGuess (nLeg, 0.0);
    }

    // ================================================================ block B: stage 2, the tone network, Volume
    {
        auto& c = ch.b;
        const auto src = c.addNode(), plus = c.addNode(), minus = c.addNode(), nLeg = c.addNode(), o2 = c.addNode();
        const auto nV = c.addNode(), nSw = c.addNode(), nW = c.addNode(), nT = c.addNode(), nT2 = c.addNode(), lw = c.addNode();
        ch.srcClip = c.addSource (src, vBias);

        // 10K into (+); (-) leg 39K + 100 nF to ground; feedback 150K || 220 pF
        c.addResistor (src, plus, 10.0e3);
        c.addOpAmpMacro (plus, minus, o2, tl082);
        c.addResistor (minus, nLeg, 39.0e3);
        c.addCapacitor (nLeg, gnd, 100.0e-9);
        c.addCapacitor (minus, o2, 220.0e-12);
        c.addResistor (minus, o2, 150.0e3);

        // 1 uF, then 33K and (22K behind the HP/LP switch) to the tone node, whose shunt branch is 47 nF + the Tone rheostat
        // + 1K to ground (the +MIDS switches of the other versions are left at v1.7: C9 bypassed, C10 out); then Volume.
        c.addCapacitor (o2, nV, 1.0e-6);
        c.addResistor (nV, nW, 33.0e3);
        c.addResistor (nV, nSw, 22.0e3);
        ch.rHp = c.addResistor (nSw, nW, 1.0);
        c.addCapacitor (nW, nT, 47.0e-9);
        ch.rTone = c.addResistor (nT, nT2, 1.0e3);
        c.addResistor (nT2, gnd, 1.0e3);
        ch.rLevelTop = c.addResistor (nW, lw, 1.0e3);
        ch.rLevelBottom = c.addResistor (lw, gnd, 1.0e3);
        c.addResistor (lw, gnd, downstreamLoad);

        ch.nOp2 = o2;
        ch.nOut = lw;
        for (auto n : { src, plus, minus, o2 })
            c.setInitialGuess (n, vBias);
        for (auto n : { nLeg, nV, nSw, nW, nT, nT2, lw })
            c.setInitialGuess (n, 0.0);
    }
}

void OCDStyleOverdriveProcessor::updatePots (double driveKnob, double toneKnob, double levelKnob)
{
    // Drive: 1M audio taper as a rheostat (with the 18K in series): more resistance = more gain.
    const double rDrive = juce::jmax (1.0, drivePotMax * pots::audio (driveKnob));

    // Tone: 10K linear, wired so that clockwise = more resistance in the treble shunt = brighter.
    const double rTone = juce::jmax (1.0, tonePotMax * toneKnob);

    // Volume: 100K linear, wiper-to-ground segment.
    const double lBottom = juce::jmax (1.0, levelPotMax * levelKnob);
    const double lTop = juce::jmax (1.0, levelPotMax - lBottom);

    for (auto& ch : channels)
    {
        ch.a.setResistance (ch.rDrive, rDrive);
        ch.b.setResistance (ch.rTone, rTone);
        ch.b.setResistance (ch.rLevelTop, lTop);
        ch.b.setResistance (ch.rLevelBottom, lBottom);
    }
}

void OCDStyleOverdriveProcessor::updateSwitches()
{
    const bool led = clipping->get() >= 0.5f;
    const bool highPeak = peak->get() >= 0.5f;

    for (auto& ch : channels)
    {
        ch.a.setResistance (ch.rClipSwitch, led ? switchOpenOhms : switchClosedOhms);
        ch.b.setResistance (ch.rHp, highPeak ? switchClosedOhms : switchOpenOhms);
    }
}

void OCDStyleOverdriveProcessor::prepare (double newSampleRate, int, int)
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

    smoothedDrive.reset (newSampleRate, 0.02);
    smoothedDrive.setCurrentAndTargetValue (drive->get());
    smoothedTone.reset (newSampleRate, 0.02);
    smoothedTone.setCurrentAndTargetValue (tone->get());
    smoothedLevel.reset (newSampleRate, 0.02);
    smoothedLevel.setCurrentAndTargetValue (level->get());

    updatePots (drive->get(), tone->get(), level->get());
    updateSwitches();

    dcOk = true;
    for (auto& ch : channels)
    {
        dcOk = ch.a.prepare (newSampleRate) && dcOk;
        ch.b.setSource (ch.srcClip, ch.a.voltage (ch.nClip));
        dcOk = ch.b.prepare (newSampleRate) && dcOk;
    }

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void OCDStyleOverdriveProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedDrive.setTargetValue (drive->get());
    smoothedTone.setTargetValue (tone->get());
    smoothedLevel.setTargetValue (level->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float d = smoothedDrive.getNextValue();
        const float t = smoothedTone.getNextValue();
        const float l = smoothedLevel.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots (d, t, l);
            updateSwitches();
        }

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            ch.a.setSource (ch.srcIn, (double) data[i]);
            bool ok = ch.a.solveSample();

            ch.b.setSource (ch.srcClip, ch.a.voltage (ch.nClip));
            ok = ch.b.solveSample() && ok;

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

void OCDStyleOverdriveProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // The shared overdrive glyph (Assets/Icons/overdrive.svg), like the other "...-Style Overdrive" pedals.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
