#include "CrunchBoxStyleDistortionProcessor.h"
#include "PotTaper.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double vBias = 4.5; // R10/R11 22K/22K + 100 uF: the drawing's "BIAS", taken as ideal

    // Red LED (the Guv'nor's / Nobels': ~1.75 V at 0.5 mA).
    constexpr double ledIs = 1.3e-19;
    constexpr double ledNVt = 1.9 * 25.85e-3;

    // LM833: 110 dB, 15 MHz gain-bandwidth, ~37 ohm out, swing ~1.5 V short of each rail of a 9 V supply.
    const NodalCircuit::OpAmpMacro lm833 { 3.16e5, 15.0e6, 37.0, 1.5, 7.5, 0.0, 0.0 };

    constexpr double drivePotMax = 100.0e3; // "100k-B" linear
    constexpr double tonePotMax = 10.0e3;   // "10k-C" reverse-log, a rheostat
    constexpr double levelPotMax = 100.0e3; // "100k-B" linear
    constexpr double downstreamLoad = 1.0e6;
}

CrunchBoxStyleDistortionProcessor::CrunchBoxStyleDistortionProcessor()
{
    auto make = [] (const char* id, const char* name)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    };

    auto driveParam = make ("crunchbox_drive", "Drive");
    auto toneParam = make ("crunchbox_tone", "Tone");
    auto levelParam = make ("crunchbox_volume", "Volume");

    drive = driveParam.get();
    tone = toneParam.get();
    level = levelParam.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "crunchbox", "Crunch Box-Style Distortion", "|", std::move (driveParam));
    group->addChild (std::move (toneParam));
    group->addChild (std::move (levelParam));
    parameters = std::move (group);
}

void CrunchBoxStyleDistortionProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ block A: the gain stage
    // 22 nF -> 1K -> (+) with 1M to the bias and 1 nF to ground. (-) leg: 1K + 0.22 uF to ground (the gain rises above
    // 720 Hz). Feedback = the Drive pot from (-) to its wiper (the op-amp output) with 100 pF across it.
    {
        auto& c = ch.a;
        const auto nb = c.addNode(), in = c.addNode(), nA = c.addNode(), plus = c.addNode(), minus = c.addNode();
        const auto nLeg = c.addNode(), out = c.addNode();
        c.addSource (nb, vBias);
        ch.srcIn = c.addSource (in, 0.0);

        c.addCapacitor (in, nA, 22.0e-9);
        c.addResistor (nA, plus, 1.0e3);
        c.addResistor (plus, nb, 1.0e6);
        c.addCapacitor (plus, gnd, 1.0e-9);
        c.addOpAmpMacro (plus, minus, out, lm833);
        c.addResistor (minus, nLeg, 1.0e3);
        c.addCapacitor (nLeg, gnd, 0.22e-6);
        c.addCapacitor (minus, out, 100.0e-12);
        ch.rDriveFeedback = c.addResistor (minus, out, 1.0e3);

        ch.nOp1 = out;
        for (auto n : { nA, plus, minus, out })
            c.setInitialGuess (n, vBias);
        c.setInitialGuess (nLeg, 0.0);
    }

    // ================================================================ block B: second stage, LEDs, Tone, Volume
    {
        auto& c = ch.b;
        const auto nb = c.addNode(), src = c.addNode(), nOut1 = c.addNode(), nSer = c.addNode(), nC = c.addNode();
        const auto minus = c.addNode(), out2 = c.addNode(), nP = c.addNode(), nL = c.addNode(), nT1 = c.addNode();
        const auto nT2 = c.addNode(), nV = c.addNode(), nS = c.addNode(), lw = c.addNode();
        c.addSource (nb, vBias);
        ch.srcOp1 = c.addSource (src, vBias);

        // op-amp 1's output resistance, the Drive pot's wiper-to-end segment, 0.1 uF, 10K into (-) of op-amp 2
        c.addResistor (src, nOut1, lm833.outputOhms);
        ch.rDriveSeries = c.addResistor (nOut1, nSer, 1.0e3);
        c.addCapacitor (nSer, nC, 0.1e-6);
        c.addResistor (nC, minus, 10.0e3);

        // op-amp 2: inverting, (+) at the bias; feedback = 1M || 100 pF
        c.addOpAmpMacro (nb, minus, out2, lm833);
        c.addResistor (minus, out2, 1.0e6);
        c.addCapacitor (minus, out2, 100.0e-12);

        // 2.2 uF -> 1K -> two red LEDs to ground -> 100 ohm -> Tone (rheostat) -> 39 nF to ground -> 10K -> (4K7 + 22 nF to
        // ground) -> Volume (100K linear, wiper to ground segment) -> the output
        c.addCapacitor (out2, nP, 2.2e-6);
        c.addResistor (nP, nL, 1.0e3);
        c.addDiode (nL, gnd, ledIs, ledNVt, 30.0e-9);
        c.addDiode (gnd, nL, ledIs, ledNVt, 30.0e-9);
        c.addResistor (nL, nT1, 100.0);
        ch.rTone = c.addResistor (nT1, nT2, 1.0e3);
        c.addCapacitor (nT2, gnd, 39.0e-9);
        c.addResistor (nT2, nV, 10.0e3);
        c.addResistor (nV, nS, 4.7e3);
        c.addCapacitor (nS, gnd, 22.0e-9);
        ch.rLevelTop = c.addResistor (nV, lw, 1.0e3);
        ch.rLevelBottom = c.addResistor (lw, gnd, 1.0e3);
        c.addResistor (lw, gnd, downstreamLoad);

        ch.nOp2 = out2;
        ch.nLed = nL;
        ch.nOut = lw;
        for (auto n : { src, nOut1, nSer, nC, minus, out2 })
            c.setInitialGuess (n, vBias);
        for (auto n : { nP, nL, nT1, nT2, nV, nS, lw })
            c.setInitialGuess (n, 0.0);
    }
}

void CrunchBoxStyleDistortionProcessor::updatePots (double driveKnob, double toneKnob, double levelKnob)
{
    // Drive: 100K linear; the wiper is the op-amp's output, so the (-) side is the feedback and the rest is a series
    // resistance into the second stage.
    const double dFb = juce::jmax (1.0, drivePotMax * driveKnob);
    const double dSer = juce::jmax (1.0, drivePotMax - dFb);

    // Tone: "C" (reverse-log) 10K as a rheostat, wired so that clockwise = less resistance = brighter.
    const double rTone = juce::jmax (1.0, tonePotMax * (1.0 - pots::reverseAudio (toneKnob)));

    // Volume: 100K linear, wiper-to-ground segment.
    const double lBottom = juce::jmax (1.0, levelPotMax * levelKnob);
    const double lTop = juce::jmax (1.0, levelPotMax - lBottom);

    for (auto& ch : channels)
    {
        ch.a.setResistance (ch.rDriveFeedback, dFb);
        ch.b.setResistance (ch.rDriveSeries, dSer);
        ch.b.setResistance (ch.rTone, rTone);
        ch.b.setResistance (ch.rLevelTop, lTop);
        ch.b.setResistance (ch.rLevelBottom, lBottom);
    }
}

void CrunchBoxStyleDistortionProcessor::prepare (double newSampleRate, int, int)
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

    dcOk = true;
    for (auto& ch : channels)
    {
        dcOk = ch.a.prepare (newSampleRate) && dcOk;
        ch.b.setSource (ch.srcOp1, ch.a.voltage (ch.nOp1));
        dcOk = ch.b.prepare (newSampleRate) && dcOk;
    }

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void CrunchBoxStyleDistortionProcessor::process (juce::AudioBuffer<float>& buffer)
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
        }

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            ch.a.setSource (ch.srcIn, (double) data[i]);
            bool ok = ch.a.solveSample();

            ch.b.setSource (ch.srcOp1, ch.a.voltage (ch.nOp1));
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

void CrunchBoxStyleDistortionProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // The shared distortion glyph (Assets/Icons/distortion.svg), like the other "...-Style Distortion" pedals.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::distortion_svg, IconData::distortion_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
