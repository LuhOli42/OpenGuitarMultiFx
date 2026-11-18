#pragma once

#include "PhaseVocoderShifter.h"
#include "SpliceShifter.h"
#include "SynthParts.h"

namespace openguitarmultifx::gs
{

/** One fixed-ratio pitch-shift voice with three engines, picked per call:
    MONO   SpliceShifter: ~5-15 ms late, exact on single notes, chords' thirds a little out;
    POLY   PhaseVocoderShifter: every note of a chord in tune, ~32 ms late;
    HYBRID each attack from MONO, then a crossfade into POLY once POLY's partials have been started in phase with MONO's
           (unaligned, the two paths are ~30 ms apart and a crossfade between them cancels).
    Switching engine restarts both from silence (the idle one was not fed). No allocation after prepare(). */
class HybridShifter
{
public:
    enum Mode { mono = 0, poly = 1, hybrid = 2 };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        splice.prepare (fs);
        vocoder.prepare (fs);
        onsetFast.set (1.0, 20.0, fs);
        onsetSlow.set (30.0, 300.0, fs);
        // align 50 ms into the note (past the pick transient, which skews POLY's frequency estimates), then a 40 ms
        // fade: measured on the DI notes and chord shapes as the smallest crossfade hole (worst ~-3 dB, median ~-0.5 dB)
        alignAt = (int) std::lround (0.05 * fs);
        handoffAt = alignAt + vocoder.getHopSamples() + vocoder.getLatencySamples();
        fadeUp = 1.0 / (0.04 * fs);
        fadeDown = 1.0 / (0.004 * fs);
        refractory = (int) std::lround (0.03 * fs);
        powA = std::exp (-1.0 / (0.015 * fs));
        compA = std::exp (-1.0 / (0.005 * fs));
        reset();
    }

    void reset()
    {
        splice.reset();
        vocoder.reset();
        resetHybrid();
        lastMode = -1;
    }

    double process (float x, double ratio, int m)
    {
        if (m != lastMode)
        {
            if (lastMode >= 0) { splice.reset(); vocoder.reset(); resetHybrid(); }
            lastMode = m;
        }
        const float monoOut = m != poly ? splice.process (x, ratio) : 0.0f;
        const float polyOut = m != mono ? vocoder.process (x, ratio, monoOut) : 0.0f;
        if (m != hybrid)
            return m == mono ? monoOut : polyOut;

        const double ef = onsetFast.process (x), es = onsetSlow.process (x);
        const bool rising = ef > 2.0 * es + 1e-4;
        if (rising && ! wasRising && sinceOnset > refractory) { sinceOnset = 0; aligned = false; }
        wasRising = rising;
        if (sinceOnset < handoffAt) ++sinceOnset;
        if (sinceOnset == alignAt) { vocoder.alignToReference(); aligned = true; }
        if (aligned && sinceOnset >= handoffAt) polyWeight = juce::jmin (1.0, polyWeight + fadeUp);
        else if (sinceOnset < alignAt) polyWeight = juce::jmax (0.0, polyWeight - fadeDown);
        const double gP = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * polyWeight);
        const double mix = monoOut + gP * (polyOut - monoOut);
        // energy compensation: whatever the two paths still cancel during the crossfade is made up, so the
        // mix keeps the level a coherent (in-phase) blend of the two would have
        pM = powA * pM + (1.0 - powA) * (double) monoOut * monoOut;
        pP = powA * pP + (1.0 - powA) * (double) polyOut * polyOut;
        pX = powA * pX + (1.0 - powA) * mix * mix;
        double target = 1.0;
        if (gP > 0.0 && gP < 1.0)
        {
            const double want = (1.0 - gP) * std::sqrt (pM) + gP * std::sqrt (pP);
            target = juce::jlimit (1.0, 2.0, want / std::sqrt (pX + 1e-12));
        }
        compGain = compA * compGain + (1.0 - compA) * target;
        return mix * compGain;
    }

private:
    void resetHybrid()
    {
        onsetFast.reset(); onsetSlow.reset();
        sinceOnset = 1 << 30; aligned = false; wasRising = false; polyWeight = 0.0;
        pM = pP = pX = 0.0; compGain = 1.0;
    }

    double fs = 48000.0;
    SpliceShifter splice;
    PhaseVocoderShifter vocoder;
    int lastMode = -1;
    EnvelopeFollower onsetFast, onsetSlow;
    int sinceOnset = 0, alignAt = 960, handoffAt = 2880, refractory = 1440;
    bool aligned = false, wasRising = false;
    double polyWeight = 0.0, fadeUp = 0.001, fadeDown = 0.005;
    double pM = 0.0, pP = 0.0, pX = 0.0, compGain = 1.0, powA = 0.999, compA = 0.99;
};

} // namespace openguitarmultifx::gs
