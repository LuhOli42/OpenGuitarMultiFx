#include "OD1StyleOverdriveProcessor.h"
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
        ch.q6.setParameters (1.0e-14, 25.85e-3, 200.0, 4.0);
        ch.q7.setParameters (1.0e-14, 25.85e-3, 200.0, 4.0);

        // 1 diode forward, 2 in series reverse -- the OD-1's documented
        // asymmetric clipping (see the doc's "Op-amp 1" section).
        ch.clipper.setParameters (diodeSaturationCurrent, diodeThermalVoltage, 1.0, 2.0);
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
    smoothedRFeedback.setCurrentAndTargetValue (r5 + juce::jmax (1.0f, driveMax * drive->get()));
    smoothedRTopToWiper.reset (newSampleRate, 0.02);
    smoothedRTopToWiper.setCurrentAndTargetValue (juce::jmax (1.0f, levelMax * (1.0f - level->get())));

    for (auto& ch : channels)
    {
        ch.c1.prepare (newSampleRate, c1Value);
        ch.c2.prepare (newSampleRate, c2Value);
        ch.c4.prepare (newSampleRate, c4Value);
        ch.c5.prepare (newSampleRate, c5Value);
        ch.c7.prepare (newSampleRate, c7Value);
        ch.c8.prepare (newSampleRate, c8Value);

        // Warm-start every nonlinear device near its approximate DC
        // operating point -- same rationale as the DS-1/booster's
        // prepare(). Q6/Q7 mirror the DS-1's Q1/Q3 emitter followers
        // exactly (base near BIAS1, emitter ~0.6V below).
        ch.q6.reset (bias1, bias1 - 0.6, supplyVoltage);
        ch.q7.reset (bias1, bias1 - 0.6, supplyVoltage);
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

    smoothedRFeedback.setTargetValue (r5 + juce::jmax (1.0f, driveMax * drive->get()));
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
            const double reqC1 = (double) s.c1.getEquivalentResistance();
            const double histC1 = (double) s.c1.getHistoryVoltage();

            const Thevenin baseInputBranch { r1 + reqC1, x - histC1 };
            const Thevenin baseBiasBranch { r2, bias1 };
            const Thevenin q6Base = combineParallel (baseInputBranch, baseBiasBranch);

            // Op-amp 1's inverting input (pin 6) is pinned at BIAS1 by the
            // op-amp's own virtual short with pin 5 (which is itself
            // provably fixed at BIAS1 -- see docs/circuits/
            // OD1StyleOverdrive.md's "Op-amp 1" section: nothing else
            // attaches to pin 5, so zero current ever flows through its
            // R/C branch and it never moves off BIAS1). This is a KNOWN,
            // constant value, unlike the DS-1's Q1/Q6-JFET coupling --
            // no delay approximation needed here at all.
            constexpr double pin6Voltage = (double) bias1;
            const double reqC2 = (double) s.c2.getEquivalentResistance();
            const double histC2 = (double) s.c2.getHistoryVoltage();
            const Thevenin emitterLocalBranch { r3, 0.0 };
            const Thevenin emitterToPin6Branch { reqC2, pin6Voltage + histC2 };
            const Thevenin q6Emitter = combineParallel (emitterLocalBranch, emitterToPin6Branch);

            const Thevenin q6Collector { 1.0e-6, supplyVoltage };

            double q6Vb, q6Ve, q6Vc;
            s.q6.solve (q6Base.rth, q6Base.vth, q6Emitter.rth, q6Emitter.vth, q6Collector.rth, q6Collector.vth,
                        q6Vb, q6Ve, q6Vc);

            const double iC1Actual = (x - histC1 - q6Vb) / (r1 + reqC1);
            s.c1.updateState ((float) (iC1Actual * reqC1 + histC1), (float) iC1Actual);
            const double vC2 = q6Ve - pin6Voltage;
            s.c2.updateState ((float) vC2, (float) ((vC2 - histC2) / reqC2));

            // ---- Op-amp 1: the clipper. Current in from C2+R4 (pin 6 is
            // KNOWN/fixed, so this is fully determined already), must all
            // flow back out through the feedback network (R5+Drive pot,
            // with the asymmetric diode pair bridged across it) -- solved
            // via AsymmetricDiodePair's own 1D Newton-Raphson. See the
            // docs file for the full derivation. ----
            const Thevenin pin6FromC2 { reqC2, q6Ve - histC2 };
            const Thevenin pin6FromR4 { r4, bias1 };
            const Thevenin pin6Input = combineParallel (pin6FromC2, pin6FromR4);
            const double iInput = (pin6Input.vth - pin6Voltage) / pin6Input.rth;

            double feedbackVoltage; // D = pin6 - op1Out
            s.clipper.solve (rFeedback, iInput * rFeedback, feedbackVoltage);
            const double op1Out = pin6Voltage - feedbackVoltage;

            // ---- Op-amp 2: fixed-gain (unity, inverting), fully linear
            // treble-cut buffer -- closed form, no Newton-Raphson (same
            // "ideal op-amp with linear feedback" reasoning as the DS-1's
            // non-inverting stage). pin 3 is fixed at BIAS1 by the exact
            // same "nothing else attached" argument as op-amp 1's pin 5. ----
            const double reqC4 = (double) s.c4.getEquivalentResistance();
            const double histC4 = (double) s.c4.getHistoryVoltage();
            const double iR7 = (op1Out - (double) bias1) / r7;
            const double gFeedback2 = 1.0 / r8 + 1.0 / reqC4;
            const double op2Out = (double) bias1 - (iR7 + histC4 / reqC4) / gFeedback2;

            const double vC4 = (double) bias1 - op2Out;
            s.c4.updateState ((float) vC4, (float) ((vC4 - histC4) / reqC4));

            // ---- Level pot + output buffer (a linear tree, no bridging
            // -- see docs). Reduced backward from Q7's base to find what
            // the Level wiper "sees" downstream, then forward-substitute
            // for the actual node voltages. ----
            const double reqC5 = (double) s.c5.getEquivalentResistance();
            const double histC5 = (double) s.c5.getHistoryVoltage();
            const double reqC7 = (double) s.c7.getEquivalentResistance();
            const double histC7 = (double) s.c7.getHistoryVoltage();

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

            const Thevenin q7Emitter { r13, 0.0 };
            const Thevenin q7Collector { 1.0e-6, supplyVoltage };

            double q7Vb, q7Ve, q7Vc;
            s.q7.solve (q7Base.rth, q7Base.vth, q7Emitter.rth, q7Emitter.vth, q7Collector.rth, q7Collector.vth,
                        q7Vb, q7Ve, q7Vc);

            const double vC5 = op2Out - levelTopVoltage;
            s.c5.updateState ((float) vC5, (float) ((vC5 - histC5) / reqC5));
            const double vC7 = levelWiperVoltage - q7Vb;
            s.c7.updateState ((float) vC7, (float) ((vC7 - histC7) / reqC7));

            const double reqC8 = (double) s.c8.getEquivalentResistance();
            const double histC8 = (double) s.c8.getHistoryVoltage();
            const double outputBranchCurrent = (q7Ve - histC8) / (reqC8 + outputLoadResistance);
            const double vC8 = outputBranchCurrent * reqC8 + histC8;
            s.c8.updateState ((float) vC8, (float) outputBranchCurrent);

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
