#include "TubeScreamerStyleOverdriveProcessor.h"
#include "IconKit.h"
#include "TheveninCombine.h"

#include <IconData.h>

namespace openguitarmultifx
{

const TubeScreamerStyleOverdriveProcessor::ModelSpec&
TubeScreamerStyleOverdriveProcessor::specFor (Model model) noexcept
{
    // TS808/TS9: output buffer R14/R15 per Geofex's chart (808 = 100ohm/10K,
    // TS9 = 470ohm/100K); everything else shared.
    // TS10, from the real TS-10 schematic: Q1's base bias comes from a
    // 9.2K/22K divider off +9V (=6.35V, decoupled by 10uF) instead of the
    // 4.5V rail; a 220ohm sits between C2 and the op-amp (+) pin's 10K bias
    // node (so it forms a ~0.98 divider -- NOT a no-op); op-amp 2's 10K
    // (R10) returns to +9V and the Level pot's grounded end to 4.5V (both
    // AC-equivalent to the TS9's arrangement, DC-different); and the Level
    // wiper feeds the bypass JFET through an extra 1uF with 510K bias
    // resistors at the JFET's two terminals.
    static const ModelSpec ts808 { "TS808-Style Overdrive", "ts808", "ts808", 100.0f, 10.0e3f, 4.5f, 0.0f,   4.5f, 0.0f, false };
    static const ModelSpec ts9   { "TS9-Style Overdrive",   "ts9",   "ts9",   470.0f, 100.0e3f, 4.5f, 0.0f,   4.5f, 0.0f, false };
    static const ModelSpec ts10  { "TS10-Style Overdrive",  "ts10",  "ts10",  470.0f, 100.0e3f, 6.346f, 220.0f, 9.0f, 4.5f, true };

    switch (model)
    {
        case Model::ts9:  return ts9;
        case Model::ts10: return ts10;
        case Model::ts808:
        default:          return ts808;
    }
}

TubeScreamerStyleOverdriveProcessor::TubeScreamerStyleOverdriveProcessor (Model model)
    : spec (specFor (model))
{
    const juce::String prefix (spec.idPrefix);

    auto driveParam = std::make_unique<juce::AudioParameterFloat> (
        prefix + "_drive", "Drive", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto toneParam = std::make_unique<juce::AudioParameterFloat> (
        prefix + "_tone", "Tone", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto levelParam = std::make_unique<juce::AudioParameterFloat> (
        prefix + "_level", "Level", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);

    drive = driveParam.get();
    tone = toneParam.get();
    level = levelParam.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        spec.groupId, spec.displayName, "|", std::move (driveParam));
    group->addChild (std::move (toneParam));
    group->addChild (std::move (levelParam));
    parameters = std::move (group);

    for (auto& ch : channels)
    {
        // 2SC1815-class small-signal NPN (Q1/Q2); Geofex quotes a typical
        // gain of ~300. Same documented-assumption status as the other
        // circuit models' transistor parameters.

        // Two silicon diodes anti-parallel, one each way: the TS family's
        // SYMMETRIC clipping. (AsymmetricDiodePair with 1/1 diodes is the
        // symmetric case -- see that class's doc comment.)
        ch.clipper.setParameters (diodeSaturationCurrent, diodeThermalVoltage * diodeIdealityFactor, 1.0, 1.0);
    }
}

void TubeScreamerStyleOverdriveProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;

    // Same-rate re-prepare is a no-op -- see DS1StyleDistortionProcessor::
    // prepare()'s identical guard: the UI's chain reorder re-prepares every
    // processor already in the chain, which would otherwise wipe live
    // circuit state and re-settle to silence under running audio (a pop).
    if (juce::exactlyEqual (settledSampleRate, newSampleRate))
        return;
    settledSampleRate = newSampleRate;

    solveCount = 0;
    solveFailures = 0;

    smoothedRDrive.reset (newSampleRate, 0.02);
    smoothedRDrive.setCurrentAndTargetValue (juce::jmax (1.0f, driveMax * drive->get() * drive->get()));
    smoothedRToneWiperToPlus.reset (newSampleRate, 0.02);
    smoothedRToneWiperToPlus.setCurrentAndTargetValue (juce::jmax (1.0f, toneMax * tone->get()));
    smoothedRLevelWiperToBottom.reset (newSampleRate, 0.02);
    smoothedRLevelWiperToBottom.setCurrentAndTargetValue (juce::jmax (1.0f, levelMax * level->get() * level->get()));

    for (auto& ch : channels)
    {
        ch.c1.prepare (newSampleRate, c1Value);
        ch.c2.prepare (newSampleRate, c2Value);
        ch.c3.prepare (newSampleRate, c3Value);
        ch.c4.prepare (newSampleRate, c4Value);
        ch.c5.prepare (newSampleRate, c5Value);
        ch.c6.prepare (newSampleRate, c6Value);
        ch.c7.prepare (newSampleRate, c7Value);
        ch.c8.prepare (newSampleRate, c8Value);
        ch.c9.prepare (newSampleRate, c9Value);
        ch.cWiper.prepare (newSampleRate, cWiperValue);

        // Warm-start the transistors near their DC operating points: with
        // a ~510K base bias to 4.5V, base sits ~0.6V under the rail and
        // the emitter a further ~0.65V below that.
        ch.clipper.reset (0.0);
    }

    // Settle fully to the true DC operating point on silence before real
    // audio arrives (same pop-fix as the booster/DS-1/OD-1). Slowest time
    // constant: C9 (10uF) against the output load -- ~0.9s for the 100K
    // TS9/TS10 output shunt, so 6s is ~6x margin.
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

        // Settling isn't real signal -- don't let it count toward the
        // convergence statistic tests read back.
        solveCount = 0;
        solveFailures = 0;
    }
}

void TubeScreamerStyleOverdriveProcessor::reset()
{
    for (auto& ch : channels)
    {
        ch.c1.reset();
        ch.c2.reset();
        ch.c3.reset();
        ch.c4.reset();
        ch.c5.reset();
        ch.c6.reset();
        ch.c7.reset();
        ch.c8.reset();
        ch.c9.reset();
        ch.cWiper.reset();
    }
}

void TubeScreamerStyleOverdriveProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) channels.size());
    const int numSamples = buffer.getNumSamples();

    smoothedRDrive.setTargetValue (juce::jmax (1.0f, driveMax * drive->get() * drive->get()));
    smoothedRToneWiperToPlus.setTargetValue (juce::jmax (1.0f, toneMax * tone->get()));
    smoothedRLevelWiperToBottom.setTargetValue (juce::jmax (1.0f, levelMax * level->get() * level->get()));

    const double rOut = 1.0 / (1.0 / (double) spec.outputShuntResistance + 1.0 / (double) outputLoadResistance);
    const double rSeriesOut = (double) spec.outputSeriesResistance;
    const double rClipperIn = (double) spec.clipperInputSeriesResistance;
    const double levelBottomV = (double) spec.levelBottomVoltage;

    for (int i = 0; i < numSamples; ++i)
    {
        // Sample-outer, channel-inner: each pot is one physical part shared
        // by both channels, so the smoothers advance once per sample.
        const double rDrive = (double) smoothedRDrive.getNextValue();
        const double rFeedback = (double) r6 + rDrive;

        const double rToneAW = juce::jmax (1.0, (double) smoothedRToneWiperToPlus.getNextValue());
        const double rToneWB = juce::jmax (1.0, (double) toneMax - rToneAW);

        const double rLevelWiperToBottom = juce::jmax (1.0, (double) smoothedRLevelWiperToBottom.getNextValue());
        const double rLevelTopToWiper = juce::jmax (1.0, (double) levelMax - rLevelWiperToBottom);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& s = channels[(size_t) ch];
            auto* data = buffer.getWritePointer (ch);
            const double x = (double) data[i];

            // ---- Q1: emitter-follower input buffer ----
            const double reqC1 = s.c1.getEquivalentResistance();
            const double histC1 = s.c1.getHistoryVoltage();

            const Thevenin baseInputBranch { r1 + reqC1, x - histC1 };
            const Thevenin baseBiasBranch { r2, (double) spec.q1BiasVoltage };
            const Thevenin q1Base = combineParallel (baseInputBranch, baseBiasBranch);

            // The emitter also feeds C2 -> op-amp 1's (+) pin, which has R5
            // to the bias rail and NOTHING else (an ideal op-amp draws no
            // input current), so C2 and R5 are simply a series load to the
            // rail. (The bypass-path JFET, "off" while the effect is
            // engaged, is not modelled -- see the doc.)
            const double reqC2 = s.c2.getEquivalentResistance();
            const double histC2 = s.c2.getHistoryVoltage();
            const Thevenin emitterLocalBranch { r3, 0.0 };
            const Thevenin emitterToOpAmpBranch { reqC2 + rClipperIn + r5, (double) bias + histC2 };
            const Thevenin q1Emitter = combineParallel (emitterLocalBranch, emitterToOpAmpBranch);

            // Q1 is an emitter follower with its collector on the rail: a buffer, nothing that clips. An ideal follower
            // (base draws no current, emitter = base - Vbe) is the same sound without a Newton solve per sample.
            const double q1Vb = q1Base.vth;
            const double q1Ve = q1Vb - followerDrop;
            s.debugQ1Vb = q1Vb;
            s.debugQ1Ve = q1Ve;

            // C1/C2 each have a plain resistor in series (R1 / R5), so the
            // branch's total voltage drop is NOT the capacitor's own
            // voltage -- derive the branch current first, then read the
            // capacitor's voltage back from it (the same fix the OD-1's
            // C1/C5/C7 needed).
            const double iC1 = (x - histC1 - q1Vb) / (r1 + reqC1);
            s.c1.updateState ((iC1 * reqC1 + histC1), iC1);

            const double iC2 = (q1Ve - (double) bias - histC2) / (reqC2 + rClipperIn + r5);
            const double opAmpPlus = (double) bias + iC2 * r5; // op-amp 1's (+) pin voltage
            s.c2.updateState ((iC2 * reqC2 + histC2), iC2);

            // ---- Op-amp 1: non-inverting clipper. The virtual short puts
            // the (-) pin at opAmpPlus; the current that voltage drives
            // through R4 + C3 to ground must ALL return through the
            // feedback network (Drive pot + R6, in parallel with C4 and
            // the diode pair) -- the network being nonlinear is why this
            // is a 1D Newton-Raphson and not closed form. ----
            const double reqC3 = s.c3.getEquivalentResistance();
            const double histC3 = s.c3.getHistoryVoltage();
            const double iFeedback = (opAmpPlus - histC3) / (reqC3 + r4);
            s.c3.updateState ((iFeedback * reqC3 + histC3), iFeedback);

            // Feedback network across v = out - (-pin): R (R6+Drive) in
            // parallel with C4's companion model and the diodes.
            //   iFeedback = v/Rf + (v - histC4)/reqC4 + Idiode(v)
            // -> Thevenin (rth, vth) of everything except the diodes.
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

            // ---- Op-amp 2 + tone control: linear, closed form. Node A is
            // op-amp 2's (+) input (R7 from op-amp 1, C5 and R10 to
            // ground/bias, the tone pot's pin 1); the virtual short holds
            // its (-) input B at the same voltage. The pot's wiper feeds
            // the R8+C6 shunt to ground. Node W (wiper) is linear in V_A,
            // which collapses node A's KCL to one equation. ----
            const double reqC5 = s.c5.getEquivalentResistance();
            const double histC5 = s.c5.getHistoryVoltage();
            const double reqC6 = s.c6.getEquivalentResistance();
            const double histC6 = s.c6.getHistoryVoltage();

            const double gPot = 1.0 / rToneAW + 1.0 / rToneWB;
            const double gShunt = 1.0 / (reqC6 + r8);
            const double alpha = gPot / (gPot + gShunt);            // V_W = alpha * V_A + beta
            const double beta = histC6 * gShunt / (gPot + gShunt);

            const double numerator = op1Out / r7 + histC5 / reqC5 + (double) spec.toneBiasRail / r10 + beta / rToneAW;
            const double denominator = 1.0 / r7 + 1.0 / reqC5 + 1.0 / r10 + (1.0 - alpha) / rToneAW;
            const double vA = numerator / denominator;
            const double vW = alpha * vA + beta;

            // Op-amp 2 supplies whatever current the pot's B side draws,
            // through R9.
            const double op2Out = vA + r9 * (vA - vW) / rToneWB;

            const double iC5 = (vA - histC5) / reqC5;
            s.c5.updateState (vA, iC5);
            const double iC6 = (vW - histC6) / (reqC6 + r8);
            s.c6.updateState ((iC6 * reqC6 + histC6), iC6);

            // ---- Level pot + output buffer. C7 + R11 feed the pot's top;
            // the wiper feeds Q2's base through the closed JFET switch and
            // C8; R12 biases the base. Thevenin-reduce the pot (both
            // segments) to the wiper, then hang the base branch off it. ----
            const double reqC7 = s.c7.getEquivalentResistance();
            const double histC7 = s.c7.getHistoryVoltage();
            const double reqC8 = s.c8.getEquivalentResistance();
            const double histC8 = s.c8.getHistoryVoltage();
            const double reqC9 = s.c9.getEquivalentResistance();
            const double histC9 = s.c9.getHistoryVoltage();

            const Thevenin levelTop { reqC7 + r11 + rLevelTopToWiper, op2Out - histC7 };
            const Thevenin levelBottom { rLevelWiperToBottom, levelBottomV };
            const Thevenin wiper = combineParallel (levelTop, levelBottom);

            // Wiper -> [TS10 only: cWiper -> node N1 (510K to 4.5V)] -> closed
            // JFET switch -> [TS10 only: node D (510K to 4.5V)] -> C8 -> Q2
            // base. Reduced source-to-load one Thevenin step at a time, then
            // back-substituted once Q2 is solved.
            const double reqCw = s.cWiper.getEquivalentResistance();
            const double histCw = s.cWiper.getHistoryVoltage();

            Thevenin n1 = wiper;
            if (spec.hasWiperCoupling)
                n1 = combineParallel (Thevenin { wiper.rth + reqCw, wiper.vth - histCw },
                                      Thevenin { jfetBiasResistance, (double) bias });

            Thevenin d { n1.rth + closedSwitchResistance, n1.vth };
            if (spec.hasWiperCoupling)
                d = combineParallel (d, Thevenin { jfetBiasResistance, (double) bias });

            const Thevenin q2BaseFromSwitch { d.rth + reqC8, d.vth - histC8 };
            const Thevenin q2BaseBias { r12, (double) bias };
            const Thevenin q2Base = combineParallel (q2BaseFromSwitch, q2BaseBias);

            const Thevenin q2EmitterLocal { r13, 0.0 };
            const Thevenin q2EmitterToOutput { rSeriesOut + reqC9 + rOut, histC9 };
            const Thevenin q2Emitter = combineParallel (q2EmitterLocal, q2EmitterToOutput);

            const double q2Vb = q2Base.vth; // output buffer: same ideal follower
            const double q2Ve = q2Vb - followerDrop;

            const double iC8 = (d.vth - histC8 - q2Vb) / (d.rth + reqC8);
            const double vD = d.vth - iC8 * d.rth;
            s.c8.updateState ((iC8 * reqC8 + histC8), iC8);

            const double iSwitch = (n1.vth - vD) / (n1.rth + closedSwitchResistance);
            const double vN1 = n1.vth - iSwitch * n1.rth;

            double iWiperOut = iSwitch; // current leaving the wiper toward the switch/output chain
            double wiperVoltage = vN1;
            if (spec.hasWiperCoupling)
            {
                iWiperOut = (wiper.vth - histCw - vN1) / (wiper.rth + reqCw);
                wiperVoltage = wiper.vth - iWiperOut * wiper.rth;
                s.cWiper.updateState ((iWiperOut * reqCw + histCw), iWiperOut);
            }

            const double iC7 = iWiperOut + (wiperVoltage - levelBottomV) / rLevelWiperToBottom;
            s.c7.updateState ((iC7 * reqC7 + histC7), iC7);

            const double iC9 = (q2Ve - histC9) / (rSeriesOut + reqC9 + rOut);
            s.c9.updateState ((iC9 * reqC9 + histC9), iC9);

            data[i] = (float) (iC9 * rOut);
        }
    }
}

void TubeScreamerStyleOverdriveProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // Same reference-sheet category/entry ("Overdrive") as OverdriveProcessor
    // and OD1StyleOverdriveProcessor -- reusing that glyph rather than
    // inventing a new one (see docs/icons/AGENT-icon-notes.md).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
