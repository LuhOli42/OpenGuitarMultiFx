#include "TS9BStyleOverdriveProcessor.h"
#include "PotTaper.h"
#include "IconKit.h"
#include "TheveninCombine.h"

#include <IconData.h>

namespace openguitarmultifx
{

TS9BStyleOverdriveProcessor::TS9BStyleOverdriveProcessor()
{
    auto makeParam = [] (const char* id, const char* name)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    };

    auto driveParam = makeParam ("ts9b_drive", "Drive");
    auto mixParam = makeParam ("ts9b_mix", "Mix");
    auto bassParam = makeParam ("ts9b_bass", "Bass");
    auto trebleParam = makeParam ("ts9b_treble", "Treble");
    auto levelParam = makeParam ("ts9b_level", "Level");

    drive = driveParam.get();
    mix = mixParam.get();
    bass = bassParam.get();
    treble = trebleParam.get();
    level = levelParam.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "ts9b", "TS9B-Style Overdrive", "|", std::move (driveParam));
    group->addChild (std::move (mixParam));
    group->addChild (std::move (bassParam));
    group->addChild (std::move (trebleParam));
    group->addChild (std::move (levelParam));
    parameters = std::move (group);

    for (auto& ch : channels)
        ch.clipper.setParameters (diodeSaturationCurrent, diodeThermalVoltage * diodeIdealityFactor, 1.0, 1.0);
}

void TS9BStyleOverdriveProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    if (juce::exactlyEqual (settledSampleRate, newSampleRate))
        return;
    settledSampleRate = newSampleRate;

    solveCount = 0;
    solveFailures = 0;

    smoothedRDrive.reset (newSampleRate, 0.02);
    smoothedRDrive.setCurrentAndTargetValue (juce::jmax (1.0f, driveMax * (float) pots::audio (drive->get())));
    smoothedRMixWet.reset (newSampleRate, 0.02);
    smoothedRMixWet.setCurrentAndTargetValue (juce::jmax (1.0f, mixSum * mix->get()));
    smoothedRTrebleWiperToPlus.reset (newSampleRate, 0.02);
    smoothedRTrebleWiperToPlus.setCurrentAndTargetValue (juce::jmax (1.0f, trebleMax * treble->get()));
    smoothedRBassWiperToPlus.reset (newSampleRate, 0.02);
    smoothedRBassWiperToPlus.setCurrentAndTargetValue (juce::jmax (1.0f, bassMax * bass->get()));
    smoothedRLevelWiperToBottom.reset (newSampleRate, 0.02);
    smoothedRLevelWiperToBottom.setCurrentAndTargetValue (juce::jmax (1.0f, levelMax * (float) pots::audio (level->get())));

    for (auto& ch : channels)
    {
        ch.c1.prepare (newSampleRate, c1Value);
        ch.c2.prepare (newSampleRate, c2Value);
        ch.c3.prepare (newSampleRate, c3Value);
        ch.c4.prepare (newSampleRate, c4Value);
        ch.c5.prepare (newSampleRate, c5Value);
        ch.c6.prepare (newSampleRate, c6Value);
        ch.c6b.prepare (newSampleRate, c6bValue);
        ch.c7.prepare (newSampleRate, c7Value);
        ch.c8.prepare (newSampleRate, c8Value);
        ch.c9.prepare (newSampleRate, c9Value);
        ch.cDry.prepare (newSampleRate, cDryValue);
        ch.clipper.reset (0.0);
    }

    // Same DC settle as the TS model (the 10uF output cap dominates, ~6x margin).
    if (newSampleRate > 0.0)
    {
        const int settleBlockSize = 512;
        juce::AudioBuffer<float> silence (2, settleBlockSize);
        int samplesRemaining = (int) (newSampleRate * 6.0);

        while (samplesRemaining > 0)
        {
            const int thisBlock = juce::jmin (settleBlockSize, samplesRemaining);
            silence.clear();
            juce::AudioBuffer<float> silenceView (silence.getArrayOfWritePointers(), 2, thisBlock);
            process (silenceView);
            samplesRemaining -= thisBlock;
        }

        solveCount = 0;
        solveFailures = 0;
    }
}

void TS9BStyleOverdriveProcessor::reset()
{
    for (auto& ch : channels)
    {
        ch.c1.reset();
        ch.c2.reset();
        ch.c3.reset();
        ch.c4.reset();
        ch.c5.reset();
        ch.c6.reset();
        ch.c6b.reset();
        ch.c7.reset();
        ch.c8.reset();
        ch.c9.reset();
        ch.cDry.reset();
    }
}

void TS9BStyleOverdriveProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) channels.size());
    const int numSamples = buffer.getNumSamples();

    smoothedRDrive.setTargetValue (juce::jmax (1.0f, driveMax * (float) pots::audio (drive->get())));
    smoothedRMixWet.setTargetValue (juce::jmax (1.0f, mixSum * mix->get()));
    smoothedRTrebleWiperToPlus.setTargetValue (juce::jmax (1.0f, trebleMax * treble->get()));
    smoothedRBassWiperToPlus.setTargetValue (juce::jmax (1.0f, bassMax * bass->get()));
    smoothedRLevelWiperToBottom.setTargetValue (juce::jmax (1.0f, levelMax * (float) pots::audio (level->get())));

    const double rOut = 1.0 / (1.0 / 100.0e3 + 1.0 / (double) outputLoadResistance); // TS9's R15 100K || downstream
    const double rSeriesOut = 470.0; // TS9's R14

    for (int i = 0; i < numSamples; ++i)
    {
        const double rDrive = (double) smoothedRDrive.getNextValue();
        const double rFeedback = (double) r6 + rDrive;

        // The mix pot is a crossfade: the wet leg's resistance falls (more drive in the sum) as the
        // knob rises; at mix = 0 only the dry leg conducts and the pedal is a clean buffer.
        const double rDryLeg = juce::jmax (1.0, (double) smoothedRMixWet.getNextValue());
        const double rWetLeg = juce::jmax (1.0, (double) mixSum + 1.0 - rDryLeg);

        const double rTrebleAW = juce::jmax (1.0, (double) smoothedRTrebleWiperToPlus.getNextValue());
        const double rTrebleWB = juce::jmax (1.0, (double) trebleMax - rTrebleAW);
        const double rBassAW = juce::jmax (1.0, (double) smoothedRBassWiperToPlus.getNextValue());
        const double rBassWB = juce::jmax (1.0, (double) bassMax - rBassAW);

        const double rLevelWiperToBottom = juce::jmax (1.0, (double) smoothedRLevelWiperToBottom.getNextValue());
        const double rLevelTopToWiper = juce::jmax (1.0, (double) levelMax - rLevelWiperToBottom);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& s = channels[(size_t) ch];
            auto* data = buffer.getWritePointer (ch);
            const double x = (double) data[i];

            // ---- Q1: emitter-follower input buffer (identical to the TS9) ----
            const double reqC1 = s.c1.getEquivalentResistance();
            const double histC1 = s.c1.getHistoryVoltage();

            const Thevenin q1Base = combineParallel (Thevenin { r1 + reqC1, x - histC1 },
                                                   Thevenin { r2, (double) bias });

            const double reqC2 = s.c2.getEquivalentResistance();
            const double histC2 = s.c2.getHistoryVoltage();
            const Thevenin emitterToOpAmpBranch { reqC2 + r5, (double) bias + histC2 };
            // The emitter also feeds the dry path's coupling cap (C_dry -> the Mix summer). Both are loads to the bias rail.
            const double reqCDry = s.cDry.getEquivalentResistance();
            const double histCDry = s.cDry.getHistoryVoltage();
            const Thevenin emitterToDryBranch { reqCDry + 10.0e3, (double) bias + histCDry };
            const Thevenin q1Emitter = combineParallel (combineParallel (Thevenin { r3, 0.0 }, emitterToOpAmpBranch),
                                                      emitterToDryBranch);

            const double q1Vb = q1Base.vth;
            const double q1Ve = q1Vb - followerDrop;
            s.debugQ1Vb = q1Vb;
            s.debugQ1Ve = q1Ve;

            const double iC1 = (x - histC1 - q1Vb) / (r1 + reqC1);
            s.c1.updateState ((iC1 * reqC1 + histC1), iC1);

            const double iC2 = (q1Ve - (double) bias - histC2) / (reqC2 + r5);
            const double opAmpPlus = (double) bias + iC2 * r5;
            s.c2.updateState ((iC2 * reqC2 + histC2), iC2);

            // ---- Op-amp 1: non-inverting clipper (identical to the TS9) ----
            const double reqC3 = s.c3.getEquivalentResistance();
            const double histC3 = s.c3.getHistoryVoltage();
            const double iFeedback = (opAmpPlus - histC3) / (reqC3 + r4);
            s.c3.updateState ((iFeedback * reqC3 + histC3), iFeedback);

            const double reqC4 = s.c4.getEquivalentResistance();
            const double histC4 = s.c4.getHistoryVoltage();
            const double gLinear = 1.0 / rFeedback + 1.0 / reqC4;
            const double feedbackRth = 1.0 / gLinear;
            const double feedbackVth = feedbackRth * (iFeedback + histC4 / reqC4);

            double feedbackVoltage;
            const bool converged = s.clipper.solve (feedbackRth, feedbackVth, feedbackVoltage);
            ++solveCount;
            if (! converged)
                ++solveFailures;

            const double op1Out = opAmpPlus + feedbackVoltage;
            s.debugOp1Out = op1Out;
            const double iC4 = (feedbackVoltage - histC4) / reqC4;
            s.c4.updateState (feedbackVoltage, iC4);

            // ---- Op-amp 2 + the two-knob tone stage. Same bridged-feedback arrangement as the TS9's
            // tone control: each pot spans the (+) node A and the (-) node B (both held at vA by the
            // virtual short), its wiper feeding its own RC shunt to ground -- the treble leg keeps the
            // TS9's 220 + 0.22uF, the bass leg carries a bigger cap so its band sits low. ----
            const double reqC5 = s.c5.getEquivalentResistance();
            const double histC5 = s.c5.getHistoryVoltage();
            const double reqC6 = s.c6.getEquivalentResistance();
            const double histC6 = s.c6.getHistoryVoltage();
            const double reqC6b = s.c6b.getEquivalentResistance();
            const double histC6b = s.c6b.getHistoryVoltage();

            const double gPotT = 1.0 / rTrebleAW + 1.0 / rTrebleWB;
            const double gShuntT = 1.0 / (reqC6 + r8);
            const double alphaT = gPotT / (gPotT + gShuntT);
            const double betaT = histC6 * gShuntT / (gPotT + gShuntT);

            const double gPotB = 1.0 / rBassAW + 1.0 / rBassWB;
            const double gShuntB = 1.0 / (reqC6b + r8b);
            const double alphaB = gPotB / (gPotB + gShuntB);
            const double betaB = histC6b * gShuntB / (gPotB + gShuntB);

            const double numerator = op1Out / r7 + histC5 / reqC5 + (double) bias / r10
                                   + betaT / rTrebleAW + betaB / rBassAW;
            const double denominator = 1.0 / r7 + 1.0 / reqC5 + 1.0 / r10
                                     + (1.0 - alphaT) / rTrebleAW + (1.0 - alphaB) / rBassAW;
            const double vA = numerator / denominator;
            const double vWt = alphaT * vA + betaT;
            const double vWb = alphaB * vA + betaB;

            // The op-amp supplies the current both pots' B sides draw, through R9.
            const double op2Out = vA + r9 * ((vA - vWt) / rTrebleWB + (vA - vWb) / rBassWB);

            const double iC5 = (vA - histC5) / reqC5;
            s.c5.updateState (vA, iC5);
            const double iC6 = (vWt - histC6) / (reqC6 + r8);
            s.c6.updateState ((iC6 * reqC6 + histC6), iC6);
            const double iC6b = (vWb - histC6b) / (reqC6b + r8b);
            s.c6b.updateState ((iC6b * reqC6b + histC6b), iC6b);

            // ---- Mix: the driven signal and the Q1-buffered dry path meet at a resistive summer.
            // The dry leg has already passed its coupling cap (treated as a series load on the emitter);
            // the cap's own drop is folded into the branch's Thevenin voltage. ----
            const Thevenin wetBranch { rWetLeg, op2Out };
            const Thevenin dryBranch { reqCDry + 10.0e3 + rDryLeg, q1Ve - histCDry + 0.62 };
            // (the +0.62 recentres the emitter signal on the 4.5V rail the wet path swings about)
            const Thevenin mixNode = combineParallel (wetBranch, dryBranch);

            const double iDry = (q1Ve - histCDry + 0.62 - mixNode.vth) / (reqCDry + 10.0e3 + rDryLeg);
            s.cDry.updateState ((iDry * reqCDry + histCDry), iDry);

            // ---- Level pot + output buffer (the TS9's own network) ----
            const double reqC7 = s.c7.getEquivalentResistance();
            const double histC7 = s.c7.getHistoryVoltage();
            const double reqC8 = s.c8.getEquivalentResistance();
            const double histC8 = s.c8.getHistoryVoltage();
            const double reqC9 = s.c9.getEquivalentResistance();
            const double histC9 = s.c9.getHistoryVoltage();

            const Thevenin levelTop { reqC7 + r11 + rLevelTopToWiper, mixNode.vth - histC7 };
            const Thevenin levelBottom { rLevelWiperToBottom, (double) bias };
            const Thevenin wiper = combineParallel (levelTop, levelBottom);

            const Thevenin d { wiper.rth + closedSwitchResistance, wiper.vth };
            const Thevenin q2Base = combineParallel (Thevenin { d.rth + reqC8, d.vth - histC8 },
                                                   Thevenin { r12, (double) bias });

            const Thevenin q2Emitter = combineParallel (Thevenin { r13, 0.0 },
                                                      Thevenin { rSeriesOut + reqC9 + rOut, histC9 });

            const double q2Vb = q2Base.vth;
            const double q2Ve = q2Vb - followerDrop;

            const double iC8 = (d.vth - histC8 - q2Vb) / (d.rth + reqC8);
            const double vD = d.vth - iC8 * d.rth;
            s.c8.updateState ((iC8 * reqC8 + histC8), iC8);

            const double iSwitch = (wiper.vth - vD) / (wiper.rth + closedSwitchResistance);
            const double wiperVoltage = wiper.vth - iSwitch * wiper.rth;
            const double iC7 = iSwitch + (wiperVoltage - (double) bias) / rLevelWiperToBottom;
            s.c7.updateState ((iC7 * reqC7 + histC7), iC7);

            const double iC9 = (q2Ve - histC9) / (rSeriesOut + reqC9 + rOut);
            s.c9.updateState ((iC9 * reqC9 + histC9), iC9);

            data[i] = (float) (iC9 * rOut);
        }
    }
}

void TS9BStyleOverdriveProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
