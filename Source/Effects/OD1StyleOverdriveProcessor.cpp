#include "OD1StyleOverdriveProcessor.h"
#include "PotTaper.h"
#include "IconKit.h"
#include "TheveninCombine.h"

#include <IconData.h>

namespace openguitarmultifx
{

OD1StyleOverdriveProcessor::OD1StyleOverdriveProcessor()
{
    auto driveParam = std::make_unique<juce::AudioParameterFloat> (
        "od1_drive", "Drive", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto levelParam = std::make_unique<juce::AudioParameterFloat> (
        "od1_level", "Level", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);

    drive = driveParam.get();
    level = levelParam.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "od1", "OD-1-Style Overdrive", "|", std::move (driveParam));
    group->addChild (std::move (levelParam));
    parameters = std::move (group);

    for (auto& ch : channels)
    {
        // Representative small-signal silicon NPN parameters (Q6/Q7) --
        // same values as the DS-1's Q1/Q2/Q3, same "documented, adjustable
        // assumption" status, see docs/circuits/OD1StyleOverdrive.md.

        // 1 diode forward, 2 in series reverse -- the OD-1's documented
        // asymmetric clipping (see the doc's "Op-amp 1" section).
        // Positive swing of the output = forward direction of the feedback network = TWO diodes (clips at ~1.2 V), the
        // negative swing ONE (~0.6 V): 'two diodes clip the positive peak, one the negative' (the SD-1/OD-1 arrangement).
        ch.clipper.setParameters (diodeSaturationCurrent, diodeThermalVoltage * diodeIdealityFactor, 2.0, 1.0);
    }
}

void OD1StyleOverdriveProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;

    // See DS1StyleDistortionProcessor::prepare()'s identical guard for the
    // full explanation: the UI's chain-grid reorder path rebuilds the
    // whole SignalGraph and re-calls prepare() on every processor already
    // in the chain (not just the one moved), at the same sample rate,
    // every time. Without this guard, that would wipe this processor's
    // live circuit state (built up from whatever real signal has been
    // flowing) and re-settle to silence right as real signal keeps
    // arriving -- an audible pop, and a real risk of the Newton-Raphson
    // solvers converging to a different (possibly near-silent) operating
    // point than the one under way. A same-rate re-prepare is a no-op.
    if (juce::exactlyEqual (settledSampleRate, newSampleRate))
        return;
    settledSampleRate = newSampleRate;

    smoothedRFeedback.reset (newSampleRate, 0.02);
    smoothedRFeedback.setCurrentAndTargetValue (r5 + juce::jmax (1.0f, driveMax * (float) pots::audio (drive->get())));
    smoothedRTopToWiper.reset (newSampleRate, 0.02);
    smoothedRTopToWiper.setCurrentAndTargetValue (juce::jmax (1.0f, levelMax * (1.0f - level->get())));

    for (auto& ch : channels)
    {
        ch.c1.prepare (newSampleRate, c1Value);
        ch.c2.prepare (newSampleRate, c2Value);
        ch.c3.prepare (newSampleRate, c3Value);
        ch.c4.prepare (newSampleRate, c4Value);
        ch.c5.prepare (newSampleRate, c5Value);
        ch.c7.prepare (newSampleRate, c7Value);
        ch.c8.prepare (newSampleRate, c8Value);

        // Warm-start every nonlinear device near its approximate DC
        // operating point -- same rationale as the DS-1/booster's
        // prepare(). Q6/Q7 mirror the DS-1's Q1/Q3 emitter followers
        // exactly (base near BIAS1, emitter ~0.6V below).
        ch.clipper.reset (0.0);
    }

    // Settle fully to the true DC operating point on silence before this
    // processor ever sees real audio -- same pop-fix approach as the
    // booster/DS-1. Slowest time constant here is the same
    // outputLoadResistance*C8 = 1Mohm*1uF = ~1 second pairing the DS-1
    // has (same assumed downstream load convention), so the same ~6x
    // margin applies.
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
    }
}

void OD1StyleOverdriveProcessor::reset()
{
    for (auto& ch : channels)
    {
        ch.c1.reset();
        ch.c2.reset();
        ch.c3.reset();
        ch.c4.reset();
        ch.c5.reset();
        ch.c7.reset();
        ch.c8.reset();
    }
}

void OD1StyleOverdriveProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) channels.size());
    const int numSamples = buffer.getNumSamples();

    smoothedRFeedback.setTargetValue (r5 + juce::jmax (1.0f, driveMax * (float) pots::audio (drive->get())));
    smoothedRTopToWiper.setTargetValue (juce::jmax (1.0f, levelMax * (1.0f - level->get())));

    for (int i = 0; i < numSamples; ++i)
    {
        // Sample-outer, channel-inner: both pots are one real, physically
        // shared component, not per-channel -- advancing the smoothers
        // once per sample (not once per channel) keeps a stereo signal's
        // two channels seeing the exact same pot value at each instant,
        // the same convention PositiveGroundBoosterProcessor's own
        // smoothedBoostPotResistance uses.
        const double rFeedback = (double) smoothedRFeedback.getNextValue();
        const double rTopToWiper = (double) smoothedRTopToWiper.getNextValue();
        const double rWiperToBottom = juce::jmax (1.0, (double) levelMax - rTopToWiper);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& s = channels[(size_t) ch];
            auto* data = buffer.getWritePointer (ch);
            const double x = (double) data[i];

            // ---- Q6: emitter-follower input buffer ----
            const double reqC1 = s.c1.getEquivalentResistance();
            const double histC1 = s.c1.getHistoryVoltage();

            const Thevenin baseInputBranch { r1 + reqC1, x - histC1 };
            const Thevenin baseBiasBranch { r2, bias1 };
            const Thevenin q6Base = combineParallel (baseInputBranch, baseBiasBranch);

            const double reqC2 = s.c2.getEquivalentResistance();
            const double histC2 = s.c2.getHistoryVoltage();

            // Q6/Q7 are emitter followers with their collectors on the rail: buffers, nothing that clips. An ideal
            // follower (base draws no current, emitter = base - Vbe) is the same sound without a Newton solve per sample.
            const double q6Vb = q6Base.vth;
            const double q6Ve = q6Vb - followerDrop;
            s.debugQ6Vb = q6Vb;
            s.debugQ6Ve = q6Ve;

            const double iC1Actual = (x - histC1 - q6Vb) / (r1 + reqC1);
            s.c1.updateState ((iC1Actual * reqC1 + histC1), iC1Actual);
            // ---- Op-amp 1: a NON-INVERTING clipper (corrected 2026-09-20, see the doc). The buffered signal
            // reaches the (+) pin through C2 with R4 biasing it to BIAS1; the (+) pin draws no current, so its
            // voltage is BIAS1 + i * R4. The follower's output resistance (1/gm = 74 ohm, in series with C2) is
            // also what damps the trapezoid rule here -- an IDEAL source straight across a capacitor rings at
            // Nyquist forever. ----
            const double iC2 = (q6Ve - (double) bias1 - histC2) / (reqC2 + followerOutputResistance + r4);
            const double opAmpPlus = (double) bias1 + iC2 * r4;
            s.c2.updateState ((iC2 * reqC2 + histC2), iC2);

            // The virtual short puts the (-) pin at opAmpPlus; that voltage drives a current through the gain
            // leg R6 + C3 (to BIAS1), and ALL of it must come from the output through the feedback network
            // (R5 + Drive, with the asymmetric diode pair across it). Gain = 1 + Zfeedback / Zleg: 1 + 33K/4.7K
            // = 8 (18 dB) at Drive min, ~180 (45 dB) at max, exactly as the SD-1/TS family.
            const double reqC3 = s.c3.getEquivalentResistance();
            const double histC3 = s.c3.getHistoryVoltage();
            const double iLeg = (opAmpPlus - (double) bias1 - histC3) / (reqC3 + rGainLeg);
            s.c3.updateState ((iLeg * reqC3 + histC3), iLeg);

            double feedbackVoltage; // out - (-) pin
            s.clipper.solve (rFeedback, iLeg * rFeedback, feedbackVoltage);
            const double op1Out = opAmpPlus + feedbackVoltage;
            s.debugOp1Out = op1Out;

            // ---- Op-amp 2: fixed-gain (unity, inverting), fully linear
            // treble-cut buffer -- closed form, no Newton-Raphson (same
            // "ideal op-amp with linear feedback" reasoning as the DS-1's
            // non-inverting stage). pin 3 is fixed at BIAS1 by the exact
            // same "nothing else attached" argument as op-amp 1's pin 5. ----
            const double reqC4 = s.c4.getEquivalentResistance();
            const double histC4 = s.c4.getHistoryVoltage();
            const double iR7 = (op1Out - (double) bias1) / r7;
            const double gFeedback2 = 1.0 / r8 + 1.0 / reqC4;
            const double op2Out = (double) bias1 - (iR7 + histC4 / reqC4) / gFeedback2;

            const double vC4 = (double) bias1 - op2Out;
            s.c4.updateState (vC4, ((vC4 - histC4) / reqC4));

            // ---- Level pot + output buffer (a linear tree, no bridging
            // -- see docs). Reduced backward from Q7's base to find what
            // the Level wiper "sees" downstream, then forward-substitute
            // for the actual node voltages. ----
            const double reqC5 = s.c5.getEquivalentResistance();
            const double histC5 = s.c5.getHistoryVoltage();
            const double reqC7 = s.c7.getEquivalentResistance();
            const double histC7 = s.c7.getHistoryVoltage();

            const Thevenin q7BaseLocal { r12, bias1 };
            const Thevenin toQ7Base { closedSwitchResistance + reqC7 + r12, bias1 + histC7 };
            const Thevenin wiperExclTop = combineParallel (Thevenin { rWiperToBottom, bias1 }, toQ7Base);
            const Thevenin levelTopFromWiperSide { rTopToWiper + wiperExclTop.rth, wiperExclTop.vth };

            const Thevenin levelTopSource { reqC5 + r10, op2Out - histC5 };
            const Thevenin levelTopActual = combineParallel (levelTopSource, levelTopFromWiperSide);
            const double levelTopVoltage = levelTopActual.vth; // combineParallel of two sources IS the actual node voltage (no further load beyond what's already included)

            const double iThroughTopToWiper = (levelTopVoltage - levelTopFromWiperSide.vth) / levelTopFromWiperSide.rth;
            const double levelWiperVoltage = levelTopVoltage - iThroughTopToWiper * rTopToWiper;

            const Thevenin q7BaseFromWiper { closedSwitchResistance + reqC7, levelWiperVoltage - histC7 };
            const Thevenin q7Base = combineParallel (q7BaseFromWiper, q7BaseLocal);

            const double q7Vb = q7Base.vth;
            const double q7Ve = q7Vb - followerDrop;

            // C5 has R10 in series (unlike a bare coupling cap) -- the
            // branch's total voltage drop (op2Out - levelTopVoltage)
            // includes R10's own IR drop too, so it is NOT C5's own
            // voltage. Derive the branch current from the combined
            // (reqC5+r10) Thevenin first, then read C5's own voltage back
            // from that current -- same fix, and same reasoning, as C1's
            // identical bug (R1 in series) found and fixed earlier this
            // session. Unlike C1's case (where R2's much larger bias
            // resistance made the error negligible), here R10 is
            // comparable in magnitude to what C5 is combined against
            // (the Level pot's own loading), so skipping this fix was
            // audible: it collapsed a discrete companion-model capacitor
            // into an effectively much-too-fast filter, cancelling most
            // of the real AC signal instead of just blocking DC.
            const double iC5Actual = (op2Out - histC5 - levelTopVoltage) / (reqC5 + r10);
            s.c5.updateState ((iC5Actual * reqC5 + histC5), iC5Actual);
            // Same fix as C5 above -- C7 has closedSwitchResistance in
            // series (Q1's modelled-as-a-wire bypass switch), so the
            // branch's full voltage drop is not C7's own voltage.
            const double iC7Actual = (levelWiperVoltage - histC7 - q7Vb) / (closedSwitchResistance + reqC7);
            s.c7.updateState ((iC7Actual * reqC7 + histC7), iC7Actual);

            const double reqC8 = s.c8.getEquivalentResistance();
            const double histC8 = s.c8.getHistoryVoltage();
            const double outputBranchCurrent = (q7Ve - histC8) / (reqC8 + outputLoadResistance);
            const double vC8 = outputBranchCurrent * reqC8 + histC8;
            s.c8.updateState (vC8, outputBranchCurrent);

            data[i] = (float) (outputBranchCurrent * (double) outputLoadResistance);
        }
    }
}

void OD1StyleOverdriveProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // Standing until the reference sheet's dedicated "Overdrive" glyph
    // (Drive category) can be re-confirmed against this specific
    // processor -- reusing OverdriveProcessor's own existing glyph since
    // both are literally the same sheet category/entry name
    // ("Overdrive"), unlike DS1StyleDistortionProcessor's "Distortion"
    // placeholder (a different, not-yet-built sheet entry). See
    // docs/icons/AGENT-icon-notes.md.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
