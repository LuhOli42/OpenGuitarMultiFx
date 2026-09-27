#include "ODR1StyleOverdriveProcessor.h"
#include "IconKit.h"
#include "NobelsCommon.h"
#include "PotTaper.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    using namespace nobels;

    constexpr double drivePotMax = 250.0e3;    // 250KA
    constexpr double spectrumPotMax = 30.0e3;  // 30KB
    constexpr double levelPotMax = 50.0e3;     // 50KA
    constexpr double supplyVolts = 9.0;

    // Q2, C2362G: a small-signal NPN. beta is an assumption (hFE for this part class is a few hundred).
    const NodalCircuit::BjtParams q2Params { 1.0e-14, 25.85e-3, 250.0, 4.0 };
}

ODR1StyleOverdriveProcessor::ODR1StyleOverdriveProcessor()
{
    auto make = [] (const char* id, const char* name)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    };

    auto drive = make ("odr1_drive", "Drive");
    auto spectrum = make ("odr1_spectrum", "Spectrum");
    auto level = make ("odr1_level", "Level");
    driveParam = drive.get();
    spectrumParam = spectrum.get();
    levelParam = level.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "odr1", "ODR-1-Style Overdrive", "|", std::move (drive));
    group->addChild (std::move (spectrum));
    group->addChild (std::move (level));
    parameters = std::move (group);
}

void ODR1StyleOverdriveProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ block A: input buffer and stage 1 (U1A)
    {
        auto& c = ch.a;
        const auto nb = c.addNode(), in = c.addNode();
        const auto n10 = c.addNode(), nP = c.addNode();
        const auto m1 = c.addNode(), o1 = c.addNode(), nW = c.addNode();
        const auto nL1 = c.addNode(), nR1 = c.addNode();
        c.addSource (nb, vBias);
        ch.srcIn = c.addSource (in, 0.0);

        // The shared JFET input buffer, then C10 0.1 uF, R10 2K7 into U1A's (+); R11 10K to ground and C11 22 nF form
        // a low-pass with R10 (2.7 kHz) as well as the high-pass with C10
        const auto nB = addInputBuffer (c, nb, in);
        c.addCapacitor (nB, n10, 0.1e-6);
        c.addResistor (n10, nP, 2.7e3);
        c.addResistor (nP, nb, 10.0e3);
        c.addCapacitor (nP, gnd, 22.0e-9);

        // U1A: non-inverting. (-) leg = (820 + 82 nF) || (1K5 + 2.2 uF): two shelves. Feedback: two 4148 back to back
        // and 120 pF straight across, and the Drive pot (250KA) between (-) and the output, its wiper tied back to
        // (-) through R14 1K8.
        c.addOpAmpMacro (nP, m1, o1, njm4558);
        c.addResistor (m1, nL1, 820.0);
        c.addCapacitor (nL1, nb, 82.0e-9);
        c.addResistor (m1, nR1, 1.5e3);
        c.addCapacitor (nR1, nb, 2.2e-6);
        c.addDiode (m1, o1, siIs, siNVt, 4.0e-9);
        c.addDiode (o1, m1, siIs, siNVt, 4.0e-9);
        c.addCapacitor (m1, o1, 120.0e-12);
        ch.rDriveIn = c.addResistor (m1, nW, 1.0e3);   // pin 7 to the wiper
        ch.rDriveOut = c.addResistor (nW, o1, 1.0e3);  // the wiper to pin 9 (the output)
        c.addResistor (m1, nW, 1.8e3);                 // R14

        ch.nOp1 = o1;
        for (auto n : { n10, nP, m1, o1, nW, nL1, nR1 })
            c.setInitialGuess (n, vBias);
    }

    // ================================================================ block B1: shunt clipper, filter, Spectrum, stage 2 (U2A)
    {
        auto& c = ch.b1;
        const auto nb = c.addNode(), src = c.addNode(), v9 = c.addNode(), nO1 = c.addNode(), n20 = c.addNode();
        const auto nD = c.addNode(), nA2 = c.addNode(), nB2 = c.addNode(), nPl = c.addNode(), nM = c.addNode();
        const auto m2 = c.addNode(), o3 = c.addNode(), nSp4 = c.addNode(), nWs = c.addNode();
        const auto nF = c.addNode(), nBase = c.addNode(), nE = c.addNode();
        c.addSource (nb, vBias);
        c.addSource (v9, supplyVolts);
        ch.srcOp1 = c.addSource (src, vBias);

        // U1A's output resistance, C20 2.2 uF, R20 12K, then C21 2.7 nF and two 4148 back to back to ground
        c.addResistor (src, nO1, njm4558.outputOhms);
        c.addCapacitor (nO1, n20, 2.2e-6);
        c.addResistor (n20, nD, 12.0e3);
        c.addCapacitor (nD, gnd, 2.7e-9);
        c.addDiode (nD, nb, siIs, siNVt, 4.0e-9);   // D3
        c.addDiode (nb, nD, siIs, siNVt, 4.0e-9);   // D4

        // R21 39K, then 82 nF into (1 nF and 12K to ground), and R23 10K into U2A's (+)
        c.addResistor (nD, nA2, 39.0e3);
        c.addCapacitor (nA2, nB2, 82.0e-9);
        c.addCapacitor (nB2, gnd, 1.0e-9);
        c.addResistor (nB2, gnd, 12.0e3);
        c.addResistor (nA2, nPl, 10.0e3);

        // U2A. (+): R24 43K to ground, and (R25 5K1 || C24 22 nF) to the Spectrum pot's pin 6. (-): R27 10K to ground,
        // feedback 20K || 560 pF, and R26 1K2 to the pot's pin 4.
        c.addResistor (nPl, nb, 43.0e3);
        c.addResistor (nPl, nM, 5.1e3);
        c.addCapacitor (nPl, nM, 22.0e-9);
        c.addOpAmpMacro (nPl, m2, o3, njm4558);
        c.addResistor (m2, nb, 10.0e3);
        c.addResistor (m2, o3, 20.0e3);
        c.addCapacitor (m2, o3, 560.0e-12);
        c.addResistor (m2, nSp4, 1.2e3);

        // The Spectrum pot (30KB): pin 4 - wiper - pin 6. Its wiper carries 27 nF to ground and, through 0.1 uF, the Q2
        // network: 8.2 nF to Q2's base (150K to ground), 2.2K to its emitter (3.3K to the battery negative). A real
        // transistor: its collector is on the 9 V rail.
        ch.rSpectrumA = c.addResistor (nSp4, nWs, 1.0e3);
        ch.rSpectrumB = c.addResistor (nWs, nM, 1.0e3);
        c.addCapacitor (nWs, gnd, 27.0e-9);
        c.addCapacitor (nWs, nF, 0.1e-6);
        c.addCapacitor (nF, nBase, 8.2e-9);
        c.addResistor (nBase, nb, 150.0e3);
        c.addResistor (nF, nE, 2.2e3);
        c.addResistor (nE, gnd, 3.3e3);
        c.addBjt (v9, nBase, nE, false, q2Params);

        ch.nOp2 = o3;
        for (auto n : { nO1, n20, nD, nA2, nB2, nPl, nM, m2, o3, nSp4, nWs, nF, nBase })
            c.setInitialGuess (n, vBias);
        c.setInitialGuess (nE, vBias - 0.65);
    }

    // ================================================================ block B2: stage 3 (U2B), Level, output
    {
        auto& c = ch.b2;
        const auto nb = c.addNode(), src = c.addNode(), nO3 = c.addNode(), nQ = c.addNode();
        const auto m3 = c.addNode(), o4 = c.addNode(), n35 = c.addNode(), n34 = c.addNode();
        const auto nLv = c.addNode(), nLw = c.addNode();
        c.addSource (nb, vBias);
        ch.srcOp2 = c.addSource (src, vBias);

        // U2A's output resistance, R32 4K7 and C31 8.2 nF (4.1 kHz) into U2B's (+)
        c.addResistor (src, nO3, njm4558.outputOhms);
        c.addResistor (nO3, nQ, 4.7e3);
        c.addCapacitor (nQ, gnd, 8.2e-9);

        // U2B: non-inverting. Leg R35 1K2 + C32 1 uF; feedback R33 22K || C33 4.7 nF || (R34 5K1 + C34 82 nF)
        c.addOpAmpMacro (nQ, m3, o4, njm4558);
        c.addResistor (m3, n35, 1.2e3);
        c.addCapacitor (n35, nb, 1.0e-6);
        c.addResistor (m3, o4, 22.0e3);
        c.addCapacitor (m3, o4, 4.7e-9);
        c.addResistor (m3, n34, 5.1e3);
        c.addCapacitor (n34, o4, 82.0e-9);

        // C30 2.2 uF into the Level pot (50KA), then the shared output stage (R43 150K, an op-amp follower, R45 150K)
        c.addCapacitor (o4, nLv, 2.2e-6);
        ch.rLevelTop = c.addResistor (nLv, nLw, 1.0e3);
        ch.rLevelBottom = c.addResistor (nLw, nb, 1.0e3);
        ch.nOut = addOutputTail (c, nb, nLw, 150.0e3, 0.0, 150.0e3);

        ch.nOp3 = o4;
        for (auto n : { nO3, nQ, m3, o4, n35, n34, nLv, nLw })
            c.setInitialGuess (n, vBias);
    }
}

void ODR1StyleOverdriveProcessor::updatePots (double drive, double spectrum, double level)
{
    // Drive: 250KA. The segment from the wiper to the output end (pin 9) is what sets the gain; it follows the 15% audio law
    // with clockwise = more. Which lug is clockwise is not on the drawing; more drive clockwise is assumed.
    const double dOut = juce::jmax (1.0, drivePotMax * pots::audio (drive));
    const double dIn = juce::jmax (1.0, drivePotMax - dOut);

    // Spectrum: 30KB linear, wiper between pin 4 (through R26 to U2A's (-)) and pin 6 (through R25 || C24 to its (+)).
    // Clockwise = the wiper toward pin 4 = brighter (assumed; the wiper network is a shunt to ground on the (-) side there, which
    // raises the treble gain, and a load on the signal on the (+) side, which cuts it).
    const double sA = juce::jmax (1.0, spectrumPotMax * (1.0 - spectrum));
    const double sB = juce::jmax (1.0, spectrumPotMax - sA);

    // Level: 50KA (15% audio law), the wiper-to-ground segment.
    const double lBottom = juce::jmax (1.0, levelPotMax * pots::audio (level));
    const double lTop = juce::jmax (1.0, levelPotMax - lBottom);

    for (auto& ch : channels)
    {
        ch.a.setResistance (ch.rDriveOut, dOut);
        ch.a.setResistance (ch.rDriveIn, dIn);
        ch.b1.setResistance (ch.rSpectrumA, sA);
        ch.b1.setResistance (ch.rSpectrumB, sB);
        ch.b2.setResistance (ch.rLevelTop, lTop);
        ch.b2.setResistance (ch.rLevelBottom, lBottom);
    }
}

void ODR1StyleOverdriveProcessor::prepare (double newSampleRate, int, int)
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
    smoothedDrive.setCurrentAndTargetValue (driveParam->get());
    smoothedSpectrum.reset (newSampleRate, 0.02);
    smoothedSpectrum.setCurrentAndTargetValue (spectrumParam->get());
    smoothedLevel.reset (newSampleRate, 0.02);
    smoothedLevel.setCurrentAndTargetValue (levelParam->get());

    updatePots (driveParam->get(), spectrumParam->get(), levelParam->get());

    dcOk = true;
    for (auto& ch : channels)
    {
        dcOk = ch.a.prepare (newSampleRate) && dcOk;
        ch.b1.setSource (ch.srcOp1, ch.a.voltage (ch.nOp1));
        dcOk = ch.b1.prepare (newSampleRate) && dcOk;
        ch.b2.setSource (ch.srcOp2, ch.b1.voltage (ch.nOp2));
        dcOk = ch.b2.prepare (newSampleRate) && dcOk;
    }

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void ODR1StyleOverdriveProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedDrive.setTargetValue (driveParam->get());
    smoothedSpectrum.setTargetValue (spectrumParam->get());
    smoothedLevel.setTargetValue (levelParam->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float d = smoothedDrive.getNextValue();
        const float s = smoothedSpectrum.getNextValue();
        const float l = smoothedLevel.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots (d, s, l);
        }

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            ch.a.setSource (ch.srcIn, (double) data[i]);
            bool ok = ch.a.solveSample();

            ch.b1.setSource (ch.srcOp1, ch.a.voltage (ch.nOp1));
            ok = ch.b1.solveSample() && ok;

            ch.b2.setSource (ch.srcOp2, ch.b1.voltage (ch.nOp2));
            ok = ch.b2.solveSample() && ok;

            data[i] = (float) ch.b2.voltage (ch.nOut);

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

void ODR1StyleOverdriveProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // The Overdrive glyph (Assets/Icons/overdrive.svg), like the other overdrives.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
