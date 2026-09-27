#include "ZendriveStyleOverdriveProcessor.h"
#include "PotTaper.h"
#include "SeriesDiodes.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double vBias = 4.5; // R6/R7 10K/10K + 47 uF ("VR"): taken as ideal

    // BAT41 Schottky: 0.25 V at 0.1 mA, 0.32 V at 1 mA (Is 13 nA, N 1.1; the datasheet's <0.45 V at 10 mA needs a series
    // resistance this leaves out).
    constexpr DiodeModel schottky { 13.0e-9, 1.1 * 25.85e-3 };

    // 1N34A germanium (the Centaur's model).
    constexpr DiodeModel germanium { 200.0e-9, 1.3 * 25.85e-3 };

    // The 2N7000's body diode (source to drain): ~0.64 V at 0.1 mA, 0.73 V at 1 mA (Is 2 pA, N 1.4). An assumption; the
    // datasheet gives only VSD < 1.5 V at 0.4 A.
    constexpr DiodeModel body { 2.0e-12, 1.4 * 25.85e-3 };

    // AD712 (JFET input): 106 dB, 4 MHz gain-bandwidth, swing ~2 V short of each rail of a 9 V supply.
    const NodalCircuit::OpAmpMacro ad712 { 2.0e5, 4.0e6, 75.0, 2.0, 7.0, 0.0, 0.0 };

    constexpr double drivePotMax = 500.0e3; // "500kB" (the original is linear)
    constexpr double voicePotMax = 10.0e3;  // "10kB"
    constexpr double tonePotMax = 50.0e3;   // "50kB"
    constexpr double levelPotMax = 100.0e3; // "100kB"
    constexpr double downstreamLoad = 1.0e6;
}

ZendriveStyleOverdriveProcessor::ZendriveStyleOverdriveProcessor()
{
    auto make = [] (const char* id, const char* name)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    };

    auto driveParam = make ("zendrive_drive", "Drive");
    auto toneParam = make ("zendrive_tone", "Tone");
    auto voiceParam = make ("zendrive_voice", "Voice");
    auto levelParam = make ("zendrive_volume", "Volume");

    drive = driveParam.get();
    tone = toneParam.get();
    voice = voiceParam.get();
    level = levelParam.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "zendrive", "Zendrive-Style Overdrive", "|", std::move (driveParam));
    group->addChild (std::move (toneParam));
    group->addChild (std::move (voiceParam));
    group->addChild (std::move (levelParam));
    parameters = std::move (group);
}

void ZendriveStyleOverdriveProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;
    auto& c = ch.c;

    const auto nb = c.addNode(), in = c.addNode(), plus = c.addNode(), minus = c.addNode(), o1 = c.addNode();
    const auto nV1 = c.addNode(), nV2 = c.addNode(), nW = c.addNode();
    const auto nT1 = c.addNode(), nT2 = c.addNode(), o2 = c.addNode();
    const auto nC = c.addNode(), nS = c.addNode(), lw = c.addNode();
    c.addSource (nb, vBias);
    ch.srcIn = c.addSource (in, 0.0);

    // Input: 470 nF -> (+) pin (470K to the bias)
    c.addCapacitor (in, plus, 470.0e-9);
    c.addResistor (plus, nb, 470.0e3);

    // The (-) leg: 1K + the Voice rheostat + 100 nF to the bias. Feedback: 100 pF; the Drive pot from the output (pin 1) to
    // (-) (pin 3), its wiper joined to (-) through 1K.
    c.addOpAmpMacro (plus, minus, o1, ad712);
    c.addResistor (minus, nV1, 1.0e3);
    ch.rVoice = c.addResistor (nV1, nV2, 1.0e3);
    c.addCapacitor (nV2, nb, 100.0e-9);
    c.addCapacitor (minus, o1, 100.0e-12);
    ch.rDriveA = c.addResistor (o1, nW, 1.0e3);
    ch.rDriveB = c.addResistor (nW, minus, 1.0e3);
    c.addResistor (nW, minus, 1.0e3);

    // The clipper across the feedback. Each 2N7000 has its gate and drain on one net, so it is a two-terminal device: the
    // channel (which needs 2 V) and the body diode. Both channels sit behind a diode that points the other way, so only the
    // body diodes conduct (docs/circuits/ZendriveStyleOverdrive.md):
    //   (-) -> BAT41 -> 1N34A -> [body diode of Q1, source to drain] -> output      (output below (-))
    //   output -> BAT41 -> [body diode of Q2, source to drain] -> (-)               (output above (-))
    // Each chain carries one current, so it is one diode (SeriesDiodes.h) and the two make a single-port clipper.
    const auto negative = seriesDiodes ({ schottky, germanium, body });
    const auto positive = seriesDiodes ({ schottky, body });
    c.addDiode (minus, o1, negative.Is, negative.nVt, 4.0e-9);
    c.addDiode (o1, minus, positive.Is, positive.nVt, 4.0e-9);

    // Tone: 10K + the Tone rheostat into 3.3 nF, then the follower (IC1B: unity gain, inside its swing here, so an ideal one)
    c.addResistor (o1, nT1, 10.0e3);
    ch.rTone = c.addResistor (nT1, nT2, 1.0e3);
    c.addCapacitor (nT2, gnd, 3.3e-9);
    c.addFollower (nT2, o2, 0.0);

    // 470 nF, 1K, the Volume pot
    c.addCapacitor (o2, nC, 470.0e-9);
    c.addResistor (nC, nS, 1.0e3);
    ch.rLevelTop = c.addResistor (nS, lw, 1.0e3);
    ch.rLevelBottom = c.addResistor (lw, gnd, 1.0e3);
    c.addResistor (lw, gnd, downstreamLoad);

    ch.nOp1 = o1;
    ch.nMinus = minus;
    ch.nTone = nT2;
    ch.nOut = lw;

    for (auto n : { plus, minus, o1, nV2, nW, nT1, nT2, o2 })
        c.setInitialGuess (n, vBias);
    c.setInitialGuess (nV1, vBias);
    for (auto n : { nC, nS, lw })
        c.setInitialGuess (n, 0.0);
}

void ZendriveStyleOverdriveProcessor::updatePots (double driveKnob, double toneKnob, double voiceKnob, double levelKnob)
{
    // Drive: 500K linear. Rf = (output-to-wiper) + (wiper-to-(-) || 1K); clockwise moves the wiper towards the (-) end.
    const double dA = juce::jmax (1.0, drivePotMax * driveKnob);
    const double dB = juce::jmax (1.0, drivePotMax - dA);

    // Voice: 10K rheostat, clockwise = less resistance = more bass and more gain range.
    const double rVoice = juce::jmax (1.0, voicePotMax * (1.0 - voiceKnob));

    // Tone: 50K rheostat, clockwise = less resistance = brighter.
    const double rTone = juce::jmax (1.0, tonePotMax * (1.0 - toneKnob));

    // Volume: 100K linear, wiper-to-ground segment.
    const double lBottom = juce::jmax (1.0, levelPotMax * levelKnob);
    const double lTop = juce::jmax (1.0, levelPotMax - lBottom);

    for (auto& ch : channels)
    {
        ch.c.setResistance (ch.rDriveA, dA);
        ch.c.setResistance (ch.rDriveB, dB);
        ch.c.setResistance (ch.rVoice, rVoice);
        ch.c.setResistance (ch.rTone, rTone);
        ch.c.setResistance (ch.rLevelTop, lTop);
        ch.c.setResistance (ch.rLevelBottom, lBottom);
    }
}

void ZendriveStyleOverdriveProcessor::prepare (double newSampleRate, int, int)
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
    smoothedVoice.reset (newSampleRate, 0.02);
    smoothedVoice.setCurrentAndTargetValue (voice->get());
    smoothedLevel.reset (newSampleRate, 0.02);
    smoothedLevel.setCurrentAndTargetValue (level->get());

    updatePots (drive->get(), tone->get(), voice->get(), level->get());

    dcOk = true;
    for (auto& ch : channels)
        dcOk = ch.c.prepare (newSampleRate) && dcOk;

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void ZendriveStyleOverdriveProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedDrive.setTargetValue (drive->get());
    smoothedTone.setTargetValue (tone->get());
    smoothedVoice.setTargetValue (voice->get());
    smoothedLevel.setTargetValue (level->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float d = smoothedDrive.getNextValue();
        const float t = smoothedTone.getNextValue();
        const float v = smoothedVoice.getNextValue();
        const float l = smoothedLevel.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots (d, t, v, l);
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

void ZendriveStyleOverdriveProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // The shared overdrive glyph (Assets/Icons/overdrive.svg), like the other "...-Style Overdrive" pedals.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
