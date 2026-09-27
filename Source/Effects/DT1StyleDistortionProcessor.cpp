#include "DT1StyleDistortionProcessor.h"
#include "IconKit.h"
#include "NobelsCommon.h"
#include "PotTaper.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    using namespace nobels;

    constexpr double distortionPotMax = 250.0e3; // 250KB
    constexpr double tonePotMax = 50.0e3;        // 50KB
    constexpr double levelPotMax = 50.0e3;       // 50KA
}

DT1StyleDistortionProcessor::DT1StyleDistortionProcessor()
{
    auto make = [] (const char* id, const char* name)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    };

    auto distortion = make ("dt1_distortion", "Distortion");
    auto tone = make ("dt1_tone", "Tone");
    auto level = make ("dt1_level", "Level");
    distortionParam = distortion.get();
    toneParam = tone.get();
    levelParam = level.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "dt1", "DT-1-Style Distortion", "|", std::move (distortion));
    group->addChild (std::move (tone));
    group->addChild (std::move (level));
    parameters = std::move (group);
}

void DT1StyleDistortionProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ block A: input buffer and stage 1
    {
        auto& c = ch.a;
        const auto nb = c.addNode(), in = c.addNode();
        const auto nP = c.addNode();
        const auto m1 = c.addNode(), o1 = c.addNode(), nX = c.addNode(), nL = c.addNode();
        c.addSource (nb, vBias);
        ch.srcIn = c.addSource (in, 0.0);

        // The shared JFET input buffer (nobels::addInputBuffer), then C10 33 nF into U1A's (+) with R10 22K to ground:
        // a 219 Hz high-pass in front of the gain stage
        const auto nB = addInputBuffer (c, nb, in);
        c.addCapacitor (nB, nP, 33.0e-9);
        c.addResistor (nP, nb, 22.0e3);

        // U1A: non-inverting. (-) leg = 0.68 uF + 3K3; feedback = R12 10K + the Distortion pot's wiper-to-end segment,
        // with the two red LEDs and 1.5 nF straight across it.
        c.addOpAmpMacro (nP, m1, o1, njm4558);
        c.addCapacitor (m1, nL, 0.68e-6);
        c.addResistor (nL, nb, 3.3e3);
        c.addResistor (m1, nX, 10.0e3);
        ch.rDistFeedback = c.addResistor (nX, o1, 1.0e3);
        c.addDiode (m1, o1, ledIs, ledNVt, 30.0e-9);
        c.addDiode (o1, m1, ledIs, ledNVt, 30.0e-9);
        c.addCapacitor (m1, o1, 1.5e-9);

        ch.nOp1 = o1;
        for (auto n : { nP, m1, o1, nX, nL })
            c.setInitialGuess (n, vBias);
    }

    // ================================================================ block B: stage 2, Tone, Level, output
    {
        auto& c = ch.b;
        const auto nb = c.addNode(), src = c.addNode(), nO1 = c.addNode(), nY = c.addNode(), nZ = c.addNode();
        const auto m2 = c.addNode(), o2 = c.addNode(), dMid = c.addNode();
        const auto nT1 = c.addNode(), nL2 = c.addNode(), nT2 = c.addNode(), nW = c.addNode(), nQ2 = c.addNode();
        const auto nLv = c.addNode(), nLw = c.addNode();
        c.addSource (nb, vBias);
        ch.srcOp1 = c.addSource (src, vBias);

        // U1A's output resistance, then the Distortion pot's OTHER segment in parallel with C13 2.2 nF, R13 3K3 and
        // C14 68 nF into stage 2's (-)
        c.addResistor (src, nO1, njm4558.outputOhms);
        ch.rDistSeries = c.addResistor (nO1, nY, 1.0e3);
        c.addCapacitor (nO1, nY, 2.2e-9);
        c.addResistor (nY, nZ, 3.3e3);
        c.addCapacitor (nZ, m2, 68.0e-9);

        // U2B: inverting, (+) at ground. Feedback = 390K || 330 pF || one 4148 (conducts when the output goes NEGATIVE:
        // ~-0.6 V) || two 4148 in series (positive: ~+1.2 V)
        c.addOpAmpMacro (nb, m2, o2, njm4558);
        c.addResistor (m2, o2, 390.0e3);
        c.addCapacitor (m2, o2, 330.0e-12);
        c.addDiode (m2, o2, siIs, siNVt, 4.0e-9);      // D4
        c.addDiode (o2, dMid, siIs, siNVt, 4.0e-9);    // D5
        c.addDiode (dMid, m2, siIs, siNVt, 4.0e-9);    // D6

        // Tone: 4.7 nF treble path to one end of the 50K pot; R20 15K -> 68 nF to ground, then 150 nF and 68K to ground,
        // to the other end; the wiper goes to Q2's gate (an ideal follower)
        c.addCapacitor (o2, nT1, 4.7e-9);
        c.addResistor (o2, nL2, 15.0e3);
        c.addCapacitor (nL2, nb, 68.0e-9);
        c.addCapacitor (nL2, nT2, 150.0e-9);
        c.addResistor (nT2, nb, 68.0e3);
        ch.rToneTreble = c.addResistor (nT1, nW, 1.0e3);
        ch.rToneBass = c.addResistor (nW, nT2, 1.0e3);
        c.addFollower (nW, nQ2, -1.2);

        // C23 1 uF into the Level pot (50KA), then the shared output stage (nobels::addOutputTail: C24, R34, Q7, R33, C40, R40 200K,
        // Q4 an emitter follower, C41 3.3 uF, R42 200K)
        c.addCapacitor (nQ2, nLv, 1.0e-6);
        ch.rLevelTop = c.addResistor (nLv, nLw, 1.0e3);
        ch.rLevelBottom = c.addResistor (nLw, nb, 1.0e3);
        const auto nOut = addOutputTail (c, nb, nLw, 200.0e3, 0.65, 200.0e3);

        ch.nOp2 = o2;
        ch.nOut = nOut;
        for (auto n : { nO1, nY, nZ, m2, o2, dMid, nT1, nL2, nT2, nW, nQ2, nLv, nLw })
            c.setInitialGuess (n, vBias);
    }
}

void DT1StyleDistortionProcessor::updatePots (double distortion, double tone, double level)
{
    // Distortion: 250K linear, wiper on stage 1's output. Clockwise = the wiper toward the end that feeds stage 1's
    // feedback, so feedback = d * 250K (more gain in stage 1) and the series segment into stage 2 = (1 - d) * 250K.
    // Which lug is clockwise is not on the drawing; more distortion clockwise is assumed.
    const double dFeedback = juce::jmax (1.0, distortionPotMax * distortion);
    const double dSeries = juce::jmax (1.0, distortionPotMax - dFeedback);

    // Tone: 50K linear, a fixed notch between the two paths. Clockwise = brighter (wiper toward the treble path).
    const double tTreble = juce::jmax (1.0, tonePotMax * (1.0 - tone));
    const double tBass = juce::jmax (1.0, tonePotMax - tTreble);

    // Level: 50KA (15% audio law), the wiper-to-ground segment.
    const double lBottom = juce::jmax (1.0, levelPotMax * pots::audio (level));
    const double lTop = juce::jmax (1.0, levelPotMax - lBottom);

    for (auto& ch : channels)
    {
        ch.a.setResistance (ch.rDistFeedback, dFeedback);
        ch.b.setResistance (ch.rDistSeries, dSeries);
        ch.b.setResistance (ch.rToneTreble, tTreble);
        ch.b.setResistance (ch.rToneBass, tBass);
        ch.b.setResistance (ch.rLevelTop, lTop);
        ch.b.setResistance (ch.rLevelBottom, lBottom);
    }
}

void DT1StyleDistortionProcessor::prepare (double newSampleRate, int, int)
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
    smoothedDistortion.setCurrentAndTargetValue (distortionParam->get());
    smoothedTone.reset (newSampleRate, 0.02);
    smoothedTone.setCurrentAndTargetValue (toneParam->get());
    smoothedLevel.reset (newSampleRate, 0.02);
    smoothedLevel.setCurrentAndTargetValue (levelParam->get());

    updatePots (distortionParam->get(), toneParam->get(), levelParam->get());

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

void DT1StyleDistortionProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedDistortion.setTargetValue (distortionParam->get());
    smoothedTone.setTargetValue (toneParam->get());
    smoothedLevel.setTargetValue (levelParam->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float d = smoothedDistortion.getNextValue();
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

void DT1StyleDistortionProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // The Distortion glyph (Assets/Icons/distortion.svg), like the DS-1 and the HM-2.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::distortion_svg, IconData::distortion_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
