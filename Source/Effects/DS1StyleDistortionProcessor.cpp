#include "DS1StyleDistortionProcessor.h"
#include "IconKit.h"
#include "TheveninCombine.h"

#include <IconData.h>

#include <cmath>
#include <utility>

namespace openguitarmultifx
{

namespace
{
    /** Solves a 4x4 linear system (Gaussian elimination with partial
        pivoting) -- sized specifically for the Tone/Level stage's 4-node
        network (ToneIn, LugA, LugB, Wiper -- see docs/circuits/
        DS1StyleDistortion.md for why this stage is a genuine bridged
        network, not a simple ladder, and needs a real linear solve rather
        than nested Thevenin reduction). Not promoted to a general utility
        header since nothing else in this project needs an NxN solve yet;
        worth extracting if a future circuit does. */
    bool solve4x4 (double A[4][4], double b[4], double x[4]) noexcept
    {
        int order[4] = { 0, 1, 2, 3 };

        for (int col = 0; col < 4; ++col)
        {
            int pivotRow = col;
            double pivotMag = std::abs (A[order[col]][col]);
            for (int row = col + 1; row < 4; ++row)
            {
                const double mag = std::abs (A[order[row]][col]);
                if (mag > pivotMag)
                {
                    pivotMag = mag;
                    pivotRow = row;
                }
            }
            if (pivotMag < 1.0e-15)
                return false;
            std::swap (order[col], order[pivotRow]);

            for (int row = col + 1; row < 4; ++row)
            {
                const double factor = A[order[row]][col] / A[order[col]][col];
                for (int k = col; k < 4; ++k)
                    A[order[row]][k] -= factor * A[order[col]][k];
                b[order[row]] -= factor * b[order[col]];
            }
        }

        for (int col = 3; col >= 0; --col)
        {
            double sum = b[order[col]];
            for (int k = col + 1; k < 4; ++k)
                sum -= A[order[col]][k] * x[k];
            x[col] = sum / A[order[col]][col];
        }
        return true;
    }
}

DS1StyleDistortionProcessor::DS1StyleDistortionProcessor()
{
    auto driveParam = std::make_unique<juce::AudioParameterFloat> (
        "ds1_drive", "Drive", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto toneParam = std::make_unique<juce::AudioParameterFloat> (
        "ds1_tone", "Tone", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto levelParam = std::make_unique<juce::AudioParameterFloat> (
        "ds1_level", "Level", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);

    drive = driveParam.get();
    tone = toneParam.get();
    level = levelParam.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "ds1", "DS-1-Style Distortion", "|", std::move (driveParam));
    group->addChild (std::move (toneParam));
    group->addChild (std::move (levelParam));
    parameters = std::move (group);

    for (auto& ch : channels)
    {
        // Representative small-signal silicon NPN parameters (Q1/Q2/Q3) --
        // same "documented, adjustable assumption" status as the
        // booster's PNP parameters, see docs/circuits/DS1StyleDistortion.md.
        ch.q1.setParameters (1.0e-14, 25.85e-3, 200.0, 4.0);
        ch.q2.setParameters (1.0e-14, 25.85e-3, 200.0, 4.0);
        ch.q3.setParameters (1.0e-14, 25.85e-3, 200.0, 4.0);

        // Representative 2SK30ATM-GR-grade N-channel JFET parameters.
        ch.q6.setParameters (3.0e-3, -2.0, 0.02);

        ch.diodePair = std::make_unique<chowdsp::wdft::DiodePairT<double, chowdsp::wdft::ResistiveVoltageSourceT<double>>> (
            ch.diodeSource, diodeSaturationCurrent, diodeThermalVoltage, 1.0);
    }
}

void DS1StyleDistortionProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;

    // The UI's chain-grid drag/reorder path rebuilds the whole SignalGraph
    // and calls prepare() again on EVERY processor already in the chain,
    // not just the one that moved (see Source/Engine/AGENTS.md) -- at the
    // SAME sample rate, every single time. Re-running the full cold-start
    // below (wipe every capacitor's history to 0V, re-seed every
    // transistor's Newton-Raphson guess, settle through silence) would
    // throw away this processor's actual live operating point -- built up
    // from whatever real signal has been flowing through it -- right as
    // that same real signal keeps arriving, producing an audible pop and,
    // worse, a real risk of a transistor's Newton-Raphson landing in a
    // different (possibly near-cutoff, near-silent) operating point than
    // the one it had converged to under the real signal, since it's now
    // converging from a silence-implied start instead. A same-rate
    // re-prepare is a no-op: every Req/derived constant below depends only
    // on sample rate, so there's nothing to recompute.
    if (juce::exactlyEqual (settledSampleRate, newSampleRate))
        return;
    settledSampleRate = newSampleRate;

    smoothedRfPot.reset (newSampleRate, 0.02);
    smoothedRfPot.setCurrentAndTargetValue (juce::jmax (1.0f, driveMax * drive->get()));
    smoothedRAtoWiper.reset (newSampleRate, 0.02);
    smoothedRAtoWiper.setCurrentAndTargetValue (juce::jmax (1.0f, toneMax * tone->get()));
    smoothedRTtoW.reset (newSampleRate, 0.02);
    smoothedRTtoW.setCurrentAndTargetValue (juce::jmax (1.0f, levelMax * (1.0f - level->get())));

    for (auto& ch : channels)
    {
        ch.c1.prepare (newSampleRate, c1Value);
        ch.c2.prepare (newSampleRate, c2Value);
        ch.c3.prepare (newSampleRate, c3Value);
        ch.c4.prepare (newSampleRate, c4Value);
        ch.c5.prepare (newSampleRate, c5Value);
        ch.c8.prepare (newSampleRate, c8Value);
        ch.c9.prepare (newSampleRate, c9Value);
        ch.c10.prepare (newSampleRate, c10Value);
        ch.c11.prepare (newSampleRate, c11Value);
        ch.c12.prepare (newSampleRate, c12Value);
        ch.c13.prepare (newSampleRate, c13Value);
        ch.c14.prepare (newSampleRate, c14Value);

        // Warm-start every nonlinear device near its approximate DC
        // operating point -- same rationale as the booster's prepare():
        // Newton-Raphson would find the real point from any reasonable
        // start, but starting close means the first processed samples
        // don't have to converge from a wild guess. See docs/circuits/
        // DS1StyleDistortion.md for where these estimates come from.
        ch.q1.reset (bias1, bias1 - 0.6, supplyVoltage);
        ch.q2.reset (0.65, 0.02, 4.0);
        ch.q3.reset (bias1, bias1 - 0.6, supplyVoltage);
        ch.q6.reset (bias1, bias1);
        ch.lastQ2Vb = 0.65;
        ch.lastQ2Vc = 4.0;
    }

    // Settle fully to the true DC operating point on silence before this
    // processor ever sees real audio -- same pop-fix approach as
    // PositiveGroundBoosterProcessor::prepare(). The slowest time
    // constant here is C14's own output coupling against the assumed
    // downstream load (outputLoadResistance*C14 = 1Mohm*1uF = ~1 second
    // -- an order of magnitude slower than any other RC in this circuit),
    // so settling needs several multiples of a full second, not one.
    if (newSampleRate > 0.0)
    {
        const int settleBlockSize = 512;
        juce::AudioBuffer<float> silence (2, settleBlockSize);
        int samplesRemaining = (int) (newSampleRate * 6.0); // ~6x the slowest time constant

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

void DS1StyleDistortionProcessor::reset()
{
    for (auto& ch : channels)
    {
        ch.c1.reset();
        ch.c2.reset();
        ch.c3.reset();
        ch.c4.reset();
        ch.c5.reset();
        ch.c8.reset();
        ch.c9.reset();
        ch.c10.reset();
        ch.c11.reset();
        ch.c12.reset();
        ch.c13.reset();
        ch.c14.reset();
    }
}

void DS1StyleDistortionProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) channels.size());
    const int numSamples = buffer.getNumSamples();

    smoothedRfPot.setTargetValue (juce::jmax (1.0f, driveMax * drive->get()));
    smoothedRAtoWiper.setTargetValue (juce::jmax (1.0f, toneMax * tone->get()));
    smoothedRTtoW.setTargetValue (juce::jmax (1.0f, levelMax * (1.0f - level->get())));

    for (int i = 0; i < numSamples; ++i)
    {
        // Sample-outer, channel-inner: all three pots are one real,
        // physically shared component each, not per-channel -- advancing
        // the smoothers once per sample (not once per channel) keeps a
        // stereo signal's two channels seeing the exact same pot value at
        // each instant, the same convention
        // PositiveGroundBoosterProcessor's own smoothedBoostPotResistance
        // uses.
        const double rfPot = (double) smoothedRfPot.getNextValue();
        const double rAtoWiper = (double) smoothedRAtoWiper.getNextValue();
        const double rBtoWiper = juce::jmax (1.0, (double) toneMax - rAtoWiper);
        const double rTtoW = (double) smoothedRTtoW.getNextValue();
        const double rWtoB = juce::jmax (1.0, (double) levelMax - rTtoW);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& s = channels[(size_t) ch];
            auto* data = buffer.getWritePointer (ch);
            const double x = (double) data[i];

            // ---- Q1: emitter-follower input buffer ----
            const double reqC1 = (double) s.c1.getEquivalentResistance();
            const double histC1 = (double) s.c1.getHistoryVoltage();
            const Thevenin baseInputBranch { r1 + reqC1, x - histC1 };
            const Thevenin baseBiasBranch { r2, bias1 };
            const Thevenin q1Base = combineParallel (baseInputBranch, baseBiasBranch);

            // Emitter loading from C2 toward Q6's node A uses node A's
            // PREVIOUS sample value -- node A is one of Q6's own two
            // simultaneous unknowns, not resolvable before Q6's own solve
            // (see docs/circuits/DS1StyleDistortion.md and
            // EbersMollBJT::getLastSolved()'s doc comment for why this
            // specific, explained delay is used).
            double nodeAPrev, nodeBPrev;
            s.q6.getLastSolved (nodeAPrev, nodeBPrev);
            const double reqC2 = (double) s.c2.getEquivalentResistance();
            const double histC2 = (double) s.c2.getHistoryVoltage();
            const Thevenin emitterLocalBranch { r3, 0.0 };
            const Thevenin emitterToNodeABranch { reqC2, nodeAPrev + histC2 };
            const Thevenin q1Emitter = combineParallel (emitterLocalBranch, emitterToNodeABranch);

            const Thevenin q1Collector { 1.0e-6, supplyVoltage };

            double q1Vb, q1Ve, q1Vc;
            s.q1.solve (q1Base.rth, q1Base.vth, q1Emitter.rth, q1Emitter.vth, q1Collector.rth, q1Collector.vth,
                        q1Vb, q1Ve, q1Vc);

            // ---- Q6: JFET used as a voltage-controlled resistor ----
            // Node A (drain): Q1's emitter (just solved) through C2, in
            // parallel with R4 to BIAS1.
            const Thevenin nodeAFromQ1 { reqC2, q1Ve - histC2 };
            const Thevenin nodeALocal { r4, bias1 };
            const Thevenin nodeAThevenin = combineParallel (nodeAFromQ1, nodeALocal);

            // Node B (source): Q2's base network, via C3's PREVIOUS
            // sample history (Q2 hasn't been solved yet this sample --
            // this is a normal forward-direction cascade, not a delay
            // approximation: C3's history term is exactly the same kind
            // of one-sample memory every capacitor companion model here
            // already carries).
            const double reqC3 = (double) s.c3.getEquivalentResistance();
            const double histC3 = (double) s.c3.getHistoryVoltage();
            const Thevenin nodeBLocal { r5, bias1 };
            // R6 (to true ground) and R7/C4 (feedback from Q2's collector,
            // one-sample-delayed, see below) also load Q2's base, and
            // hence node B through C3 -- fold them in here so node B's
            // Thevenin is accurate, not just R5's contribution.
            const double reqC4 = (double) s.c4.getEquivalentResistance();
            const double histC4 = (double) s.c4.getHistoryVoltage();
            const Thevenin q2BaseR6 { r6, 0.0 };
            const Thevenin q2BaseR7 { r7, s.lastQ2Vc };
            const Thevenin q2BaseC4 { reqC4, s.lastQ2Vc + histC4 };
            const Thevenin q2BaseLocal = combineParallel (q2BaseR6, combineParallel (q2BaseR7, q2BaseC4));

            // node B feeds Q2's base through C3; Q2's base network (R6 ||
            // R7/C4-feedback, using last sample's collector -- see above)
            // is what node B "sees" beyond C3. Combine node B's own local
            // branch (R5) with the C3-plus-Q2-base branch (C3 in series
            // with Q2's base Thevenin).
            const Thevenin nodeBSeries { reqC3 + q2BaseLocal.rth, q2BaseLocal.vth + histC3 };
            const Thevenin nodeBThevenin = combineParallel (nodeBLocal, nodeBSeries);

            double nodeA, nodeB;
            s.q6.solve (bias1, nodeAThevenin.rth, nodeAThevenin.vth, nodeBThevenin.rth, nodeBThevenin.vth, nodeA, nodeB);

            // ---- Q2: common-emitter gain stage with shunt feedback ----
            const Thevenin q2BaseFromNodeB { reqC3, nodeB - histC3 };
            const Thevenin q2Base = combineParallel (q2BaseFromNodeB, q2BaseR6, combineParallel (q2BaseR7, q2BaseC4));

            const Thevenin q2Emitter { r9, 0.0 };

            const Thevenin q2CollectorR8 { r8, supplyVoltage };
            const Thevenin q2CollectorR7 { r7, s.lastQ2Vb };
            const Thevenin q2CollectorC4 { reqC4, s.lastQ2Vb - histC4 };
            const Thevenin q2Collector = combineParallel (q2CollectorR8, combineParallel (q2CollectorR7, q2CollectorC4));

            double q2Vb, q2Ve, q2Vc;
            s.q2.solve (q2Base.rth, q2Base.vth, q2Emitter.rth, q2Emitter.vth, q2Collector.rth, q2Collector.vth,
                        q2Vb, q2Ve, q2Vc);

            // Update every capacitor touched by the pregain so far, now
            // that both ends of each are known this sample. C1 has R1 in
            // series (unlike every other coupling cap here, which connects
            // its two nodes directly) -- "x - q1Vb" is the combined R1+C1
            // branch's voltage, NOT C1's own terminal voltage, so the
            // branch current has to be derived first and C1's own voltage
            // read back from that (same pattern as C8/C14 below, which
            // also sit in series with a plain resistor).
            const double iC1 = (x - histC1 - q1Vb) / (r1 + reqC1);
            const double vC1 = iC1 * reqC1 + histC1;
            s.c1.updateState ((float) vC1, (float) iC1);
            const double vC2 = q1Ve - nodeA;
            s.c2.updateState ((float) vC2, (float) ((vC2 - histC2) / reqC2));
            const double vC3 = nodeB - q2Vb;
            s.c3.updateState ((float) vC3, (float) ((vC3 - histC3) / reqC3));
            const double vC4 = q2Vb - q2Vc;
            s.c4.updateState ((float) vC4, (float) ((vC4 - histC4) / reqC4));
            s.lastQ2Vb = q2Vb;
            s.lastQ2Vc = q2Vc;

            // ---- Op-amp gain stage (ideal, closed form) ----
            // Non-inverting input: Q2's collector through C5, in
            // parallel with R11 to BIAS1. The op-amp draws no input
            // current, so this parallel combine's open-circuit voltage
            // IS v+ exactly (see docs/circuits/DS1StyleDistortion.md).
            const double reqC5 = (double) s.c5.getEquivalentResistance();
            const double histC5 = (double) s.c5.getHistoryVoltage();
            const Thevenin vPlusFromQ2 { reqC5, q2Vc - histC5 };
            const Thevenin vPlusLocal { r11, bias1 };
            const Thevenin vPlusThevenin = combineParallel (vPlusFromQ2, vPlusLocal);
            const double vPlus = vPlusThevenin.vth; // I == 0 at an ideal op-amp input

            const double reqC8 = (double) s.c8.getEquivalentResistance();
            const double histC8 = (double) s.c8.getHistoryVoltage();
            const double rgTotal = r13 + reqC8;
            const double iRg = (vPlus - histC8) / rgTotal;
            const double nodeMid = vPlus + rfPot * iRg;

            // Update C5 now that both Q2's collector and v+ are known.
            const double iC5 = (q2Vc - vPlus - histC5) / reqC5;
            s.c5.updateState ((float) (q2Vc - vPlus), (float) iC5);
            const double vC8 = iRg * reqC8 + histC8;
            s.c8.updateState ((float) vC8, (float) iRg);

            // ---- Diode clipper (chowdsp_wdf DiodePairT) ----
            const double reqC9 = (double) s.c9.getEquivalentResistance();
            const double histC9 = (double) s.c9.getHistoryVoltage();
            const double diodeVthRelative = (nodeMid - histC9) - (double) bias1;
            s.diodeSource.setResistanceValue (reqC9);
            s.diodeSource.setVoltage (diodeVthRelative);
            s.diodePair->incident (s.diodeSource.reflected());
            s.diodeSource.incident (s.diodePair->reflected());
            const double nodeClipRelative = chowdsp::wdft::voltage<double> (s.diodeSource);
            const double nodeClip = nodeClipRelative + (double) bias1;

            const double vC9 = nodeMid - nodeClip;
            s.c9.updateState ((float) vC9, (float) ((vC9 - histC9) / reqC9));

            // ---- Tone/Level stage (passive, Big Muff-style, genuine
            // 4-node bridged network -- see docs/circuits/
            // DS1StyleDistortion.md for why this needs a real linear
            // solve, not nested Thevenin reduction) ----
            const double reqC10 = (double) s.c10.getEquivalentResistance();
            const double histC10 = (double) s.c10.getHistoryVoltage();
            const double reqC11 = (double) s.c11.getEquivalentResistance();
            const double histC11 = (double) s.c11.getHistoryVoltage();
            const double reqC12 = (double) s.c12.getEquivalentResistance();
            const double histC12 = (double) s.c12.getHistoryVoltage();
            const double reqC13 = (double) s.c13.getEquivalentResistance();
            const double histC13 = (double) s.c13.getHistoryVoltage();

            // Everything downstream of the TONE wiper (R15, the LEVEL
            // pot, Q7's closed switch, R18, C13, Q3's own R19-to-BIAS1
            // base bias) is a genuine tree -- no loop back into the Tone
            // core -- so it reduces exactly via nested Thevenin
            // combination, worked out from Q3's base backward.
            const Thevenin q3BaseLocal { r19, bias1 };
            const Thevenin preC13ToQ3Base { r19 + reqC13, bias1 + histC13 };
            const Thevenin preC13R18 { r18, supplyVoltage };
            const Thevenin preC13Local = combineParallel (preC13ToQ3Base, preC13R18);
            const Thevenin levelWiperDownstream { preC13Local.rth + closedSwitchResistance, preC13Local.vth };
            const Thevenin levelWiperExclT = combineParallel (levelWiperDownstream, Thevenin { rWtoB, 0.0 });
            const Thevenin levelTopDownstream { rTtoW + levelWiperExclT.rth, levelWiperExclT.vth };
            const Thevenin toneWiperDownstream { r15 + levelTopDownstream.rth, levelTopDownstream.vth };

            // The Tone core itself (ToneIn, LugA, LugB, Wiper) genuinely
            // loops (Wiper connects to both LugA and LugB, which both
            // connect back to ToneIn) -- solved as one 4x4 linear system.
            double A[4][4] = {};
            double bRhs[4] = {};

            const double gSource = 1.0 / reqC10;
            A[0][0] += gSource;
            bRhs[0] += gSource * (nodeClip - histC10);

            const double gR16 = 1.0 / r16;
            A[0][0] += gR16;
            A[1][1] += gR16;
            A[0][1] -= gR16;
            A[1][0] -= gR16;

            const double gC12 = 1.0 / reqC12;
            A[1][1] += gC12;
            bRhs[1] += gC12 * histC12;

            const double gC11 = 1.0 / reqC11;
            A[0][0] += gC11;
            A[2][2] += gC11;
            A[0][2] -= gC11;
            A[2][0] -= gC11;
            bRhs[0] += gC11 * histC11;
            bRhs[2] -= gC11 * histC11;

            const double gR17 = 1.0 / r17;
            A[2][2] += gR17;

            const double gAtoWiper = 1.0 / rAtoWiper;
            A[1][1] += gAtoWiper;
            A[3][3] += gAtoWiper;
            A[1][3] -= gAtoWiper;
            A[3][1] -= gAtoWiper;

            const double gBtoWiper = 1.0 / rBtoWiper;
            A[2][2] += gBtoWiper;
            A[3][3] += gBtoWiper;
            A[2][3] -= gBtoWiper;
            A[3][2] -= gBtoWiper;

            const double gDown = 1.0 / toneWiperDownstream.rth;
            A[3][3] += gDown;
            bRhs[3] += gDown * toneWiperDownstream.vth;

            double nodeVoltages[4] = { nodeClip, nodeClip, nodeClip, nodeClip }; // fallback if solve fails
            solve4x4 (A, bRhs, nodeVoltages);
            const double toneIn = nodeVoltages[0];
            const double lugA = nodeVoltages[1];
            const double lugB = nodeVoltages[2];
            const double toneWiper = nodeVoltages[3];

            s.c10.updateState ((float) (nodeClip - toneIn), (float) ((nodeClip - histC10 - toneIn) / reqC10));
            s.c11.updateState ((float) (toneIn - lugB), (float) ((toneIn - histC11 - lugB) / reqC11));
            s.c12.updateState ((float) lugA, (float) ((lugA - histC12) / reqC12));

            // Forward-substitute from the Tone wiper out to preC13, now
            // that the Tone wiper's actual voltage is known.
            const double iDown = (toneWiper - toneWiperDownstream.vth) / toneWiperDownstream.rth;
            const double levelTop = toneWiper - iDown * r15;
            const double levelWiper = levelTop - iDown * rTtoW;

            const Thevenin preC13FromLevel { closedSwitchResistance, levelWiper };
            const Thevenin preC13 = combineParallel (preC13FromLevel, preC13R18, preC13ToQ3Base);
            const double preC13Voltage = preC13.vth;

            // ---- Q3: emitter-follower output buffer ----
            const Thevenin q3BaseFromC13 { reqC13, preC13Voltage - histC13 };
            const Thevenin q3Base = combineParallel (q3BaseFromC13, q3BaseLocal);
            const Thevenin q3Emitter { r21, 0.0 };
            const Thevenin q3Collector { 1.0e-6, supplyVoltage };

            double q3Vb, q3Ve, q3Vc;
            s.q3.solve (q3Base.rth, q3Base.vth, q3Emitter.rth, q3Emitter.vth, q3Collector.rth, q3Collector.vth,
                        q3Vb, q3Ve, q3Vc);

            const double vC13 = preC13Voltage - q3Vb;
            s.c13.updateState ((float) vC13, (float) ((vC13 - histC13) / reqC13));

            const double reqC14 = (double) s.c14.getEquivalentResistance();
            const double histC14 = (double) s.c14.getHistoryVoltage();
            const double outputBranchCurrent = (q3Ve - histC14) / (reqC14 + outputLoadResistance);
            const double vC14 = outputBranchCurrent * reqC14 + histC14;
            s.c14.updateState ((float) vC14, (float) outputBranchCurrent);

            data[i] = (float) (outputBranchCurrent * (double) outputLoadResistance);
        }
    }
}

void DS1StyleDistortionProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // Standing until the reference sheet's dedicated "Distortion" glyph
    // (Drive category) is confirmed -- see docs/icons/AGENT-icon-notes.md.
    // Same category as Overdrive, so its glyph is the least-wrong
    // placeholder available rather than inventing new geometry, which
    // this project's icon rule explicitly forbids without the sheet.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
