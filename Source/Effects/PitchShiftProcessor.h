#pragma once

#include "EffectProcessor.h"

#include <array>
#include <cmath>
#include <vector>

namespace openguitarmultifx
{

/**
    A fixed, user-set pitch shift (+/-24 semitones) via the same two-grain
    time-domain shifter PitchModProcessor generalises from
    ShimmerReverbProcessor -- but with a STATIC ratio (no LFO) and the
    standard dry/wet crossfade Mix, so at full wet the output is simply
    the input transposed to a new fixed pitch. That crossfade-to-replace
    behaviour is what distinguishes this from OctaverProcessor and
    HarmonizerProcessor, which both always keep the dry signal fully
    present and only ADD a shifted layer underneath -- a pitch shifter is
    meant to be able to fully replace the original note, an octaver/
    harmonizer never should.

    Period-locked grains: the two read pointers are offset by half the
    grain, so the summed output is a comb filter -- for a periodic input
    that cancels every frequency whose period is an odd multiple of the
    offset, and every grain wrap splices at an arbitrary phase. Neither
    sibling notices because they always mix in the dry signal; at full
    wet this processor exposes it, worst in the bass where the notch
    spacing covers whole octaves and whole shifted notes land on nulls.
    The fix real pitch shifters use is a grain length locked to the input
    period: with grain = 2 periods the half-grain offset is exactly one
    period (both grains always in phase, for every harmonic) and each
    wrap jumps exactly two periods (a splice with no phase error). A
    small decimated YIN-style tracker estimates the period on the audio
    thread; when the signal isn't periodic the grain falls back to the
    original fixed 50 ms, i.e. identical behaviour to before.
*/
class PitchShiftProcessor : public EffectProcessor
{
public:
    PitchShiftProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Pitch Shift"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff4a9e5c); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    struct GrainShifter
    {
        static constexpr float defaultGrainSeconds = 0.05f;
        static constexpr float maxGrainSeconds = 0.10f; // covers two 20 Hz periods

        std::vector<float> buffer;
        int bufferSize = 0;
        int writePos = 0;
        float grainSamples = 1.0f;
        float delayA = 0.0f;
        float delayB = 0.0f;

        void prepare (double sampleRate)
        {
            grainSamples = (float) (defaultGrainSeconds * sampleRate);
            bufferSize = (int) (maxGrainSeconds * sampleRate) * 2 + 8;
            buffer.assign ((size_t) bufferSize, 0.0f);
            clear();
        }

        void clear()
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            writePos = 0;
            delayA = grainSamples * 0.5f;
            delayB = 0.0f;
        }

        static float windowGain (float delay, float grain) noexcept
        {
            const float t = delay / grain;
            return 1.0f - std::abs (2.0f * t - 1.0f);
        }

        // Moves the grain length toward target while preserving both
        // windows' phases: scaling the read delays by the same factor
        // keeps the half-grain offset, so the only artefact is a slow
        // glide toward the new window length rather than a splice.
        void slewGrain (float target) noexcept
        {
            const float diff = target - grainSamples;
            if (diff > 0.01f || diff < -0.01f)
            {
                const float next = grainSamples + diff * 0.0007f;
                const float scale = next / grainSamples;
                grainSamples = next;
                delayA *= scale;
                delayB *= scale;
                while (delayA >= grainSamples) delayA -= grainSamples;
                while (delayA < 0.0f) delayA += grainSamples;
                while (delayB >= grainSamples) delayB -= grainSamples;
                while (delayB < 0.0f) delayB += grainSamples;
            }
        }

        float readAt (float delaySamples) const noexcept
        {
            float readPos = (float) writePos - delaySamples;
            while (readPos < 0.0f)
                readPos += (float) bufferSize;
            const int i0 = (int) readPos;
            const int i1 = (i0 + 1 >= bufferSize) ? 0 : i0 + 1;
            const float frac = readPos - (float) i0;
            return buffer[(size_t) i0] + frac * (buffer[(size_t) i1] - buffer[(size_t) i0]);
        }

        float process (float input, float ratio) noexcept
        {
            buffer[(size_t) writePos] = input;

            const float outA = readAt (delayA) * windowGain (delayA, grainSamples);
            const float outB = readAt (delayB) * windowGain (delayB, grainSamples);

            const float step = ratio - 1.0f;
            delayA -= step;
            if (delayA <= 0.0f)
                delayA += grainSamples;
            else if (delayA >= grainSamples)
                delayA -= grainSamples;

            delayB -= step;
            if (delayB <= 0.0f)
                delayB += grainSamples;
            else if (delayB >= grainSamples)
                delayB -= grainSamples;

            if (++writePos >= bufferSize)
                writePos = 0;
            return outA + outB;
        }
    };

    // Audio-thread period estimate behind the adaptive grain. A full YIN
    // pass is too heavy per-sample, so the input is low-passed and 4x
    // decimated, and the difference function is evaluated only every
    // `evalEvery` decimated samples (~6 ms) on a ~23 ms window. The
    // cumulative-mean-normalised pick avoids the classic autocorrelation
    // octave error on harmonic-rich guitar. Everything is fixed-size --
    // no allocation, no locks.
    struct PeriodTracker
    {
        static constexpr int decimation = 4;
        static constexpr int ringSize = 1024;
        static constexpr int analysisLen = 256;
        static constexpr int evalEvery = 64;      // decimated samples between analyses
        static constexpr float cmndThreshold = 0.5f;
        static constexpr float minFreqHz = 40.0f; // ~E1 -- below this a lock helps nobody
        static constexpr float maxFreqHz = 700.0f;
        static constexpr int maxTau = 512;        // lag budget: fsd/minFreqHz up to ~96k input
        static constexpr int fineRingSize = 3072;
        static constexpr int fineWindow = 1024;

        float lp = 0.0f;
        float lpCoeff = 0.0f;
        int decimPhase = 0;
        int evalCount = 0;
        std::array<float, ringSize> ring {};
        int writePos = 0;
        int tauMin = 1, tauMax = maxTau;
        std::array<float, analysisLen> window {};

        // Full-rate copy of the LP'd input for the refinement pass. The
        // decimated difference function alone can't find the period to
        // better than ~5%: decimating a period that isn't an integer number
        // of decimated samples makes the signal aperiodic in that domain,
        // so its valley lands flat and off-centre. A ~8% period error
        // audibly detunes the shifted note (the grain-length mechanism
        // above), so the coarse pick is refined at full rate.
        std::array<float, fineRingSize> fine {};
        int fineWritePos = 0;

        float periodSamples = 0.0f; // full-rate units
        int stableCount = 0;
        float frozenPeriod = 0.0f;
        int driftCount = 0;
        float grainTarget = 1.0f;
        float defaultGrain = 1.0f;
        double sr = 0.0;

        void prepare (double sampleRate)
        {
            sr = sampleRate;
            const double fsd = sampleRate / decimation;
            lpCoeff = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 1200.0f / (float) sampleRate);
            tauMin = juce::jmax (1, (int) (fsd / maxFreqHz));
            tauMax = juce::jmin (maxTau, (int) (fsd / minFreqHz));
            defaultGrain = (float) (GrainShifter::defaultGrainSeconds * sampleRate);
            grainTarget = defaultGrain;
            periodSamples = 0.0f;
            stableCount = 0;
            frozenPeriod = 0.0f;
            driftCount = 0;
            lp = 0.0f;
            decimPhase = 0;
            evalCount = 0;
            writePos = 0;
            fineWritePos = 0;
            ring.fill (0.0f);
            fine.fill (0.0f);
        }

        void push (float x) noexcept
        {
            lp += lpCoeff * (x - lp);
            fine[(size_t) fineWritePos] = lp;
            fineWritePos = (fineWritePos + 1 >= fineRingSize) ? 0 : fineWritePos + 1;
            if (++decimPhase < decimation)
                return;
            decimPhase = 0;
            ring[(size_t) writePos] = lp;
            writePos = (writePos + 1 >= ringSize) ? 0 : writePos + 1;
            if (++evalCount >= evalEvery)
            {
                evalCount = 0;
                analyse();
            }
        }

        void analyse() noexcept
        {
            // Window = the most recent analysisLen decimated samples; each
            // candidate lag compares it against the window `tau` samples
            // FURTHER INTO THE PAST. (Comparing forward would run the
            // reference off the end of the ring into stale data.)
            const int winStart = ((writePos - analysisLen) % ringSize + ringSize) % ringSize;
            double level = 0.0;
            for (int i = 0; i < analysisLen; ++i)
            {
                const float v = ring[(size_t) ((winStart + i) % ringSize)];
                window[(size_t) i] = v;
                level += (double) v * (double) v;
            }
            level = std::sqrt (level / analysisLen);
            if (level < 0.002) // silence -- no reliable period
            {
                stableCount = 0;
                periodSamples = 0.0f;
                return;
            }

            int bestTau = 0;
            double acc = 0.0;
            bool below = false;
            float bestCmnd = 1.0e9f;
            for (int tau = 1; tau <= tauMax; ++tau)
            {
                double sum = 0.0;
                const int off = ((writePos - analysisLen - tau) % ringSize + ringSize) % ringSize;
                for (int i = 0; i < analysisLen; ++i)
                {
                    const float d = window[(size_t) i] - ring[(size_t) ((off + i) % ringSize)];
                    sum += (double) d * (double) d;
                }
                const float diff = (float) (sum / analysisLen);
                acc += (double) diff;
                const float cmnd = acc > 0.0 ? (float) ((double) diff * tau / acc) : 1.0f;

                if (! below)
                {
                    if (tau >= tauMin && cmnd < cmndThreshold)
                    {
                        below = true;
                        bestCmnd = cmnd;
                        bestTau = tau;
                    }
                }
                else if (cmnd <= bestCmnd)
                {
                    bestCmnd = cmnd;
                    bestTau = tau;
                }
                else
                {
                    break; // local minimum found
                }
            }

            if (bestTau == 0)
            {
                stableCount = 0;
                periodSamples = 0.0f;
                return;
            }

            const float p = refinePeriod ((float) bestTau * decimation);
            if (periodSamples > 0.0f && std::abs (p - periodSamples) < 0.12f * periodSamples)
            {
                periodSamples = 0.7f * periodSamples + 0.3f * p;
                if (stableCount < 16)
                    ++stableCount;
            }
            else
            {
                periodSamples = p;
                stableCount = 0;
            }

            if (stableCount >= 6) // enough evals for the smoothed estimate to settle
            {
                // Freeze the locked period and only re-lock when the input
                // has genuinely moved to a new note -- a drifted estimate is
                // what a mid-note vibrato looks like, and slewing the grain
                // for it would sound like detune.
                if (frozenPeriod <= 0.0f)
                {
                    frozenPeriod = periodSamples;
                    driftCount = 0;
                }
                else if (std::abs (periodSamples - frozenPeriod) > 0.10f * frozenPeriod)
                {
                    if (++driftCount >= 3)
                    {
                        frozenPeriod = periodSamples;
                        driftCount = 0;
                    }
                }
                else
                {
                    driftCount = 0;
                }

                const float g = 2.0f * frozenPeriod;
                const float lo = 0.006f * (float) sr;
                const float hi = 0.080f * (float) sr;
                grainTarget = (g >= lo && g <= hi) ? g : defaultGrain;
            }
        }

        // Full-rate refinement of the decimated coarse pick: scans integer
        // lags +/-8% (capped) around the candidate against the last ~1.25
        // periods of the LP'd input, then parabolic-interpolates the local
        // minimum. At full rate the signal IS periodic on its own sample
        // grid, so the valley is sharp and lands within ~0.5 samples of the
        // true period -- roughly 20x more precise than the coarse pass.
        float refinePeriod (float coarse) const noexcept
        {
            const int l0 = (int) std::lround (coarse);
            const int span = juce::jmin (l0 / 8, 64);
            const int w = juce::jmin ((int) (1.25f * (float) l0), fineWindow);
            const int lo = juce::jmax (8, l0 - span);
            const int hi = juce::jmin (fineRingSize - w - 1, l0 + span);
            if (hi - lo < 4 || w < 32)
                return coarse;

            const int j = fineWritePos - 1;
            const int top = j - w + 1; // first window sample (absolute)
            auto at = [&] (int absIdx) noexcept -> float
            {
                int m = absIdx % fineRingSize;
                if (m < 0)
                    m += fineRingSize;
                return fine[(size_t) m];
            };

            int bestL = 0;
            float bestD = 1.0e30f;
            for (int L = lo; L <= hi; L += 2)
            {
                double sum = 0.0;
                for (int i = 0; i < w; ++i)
                {
                    const float d = at (top + i) - at (top + i - L);
                    sum += (double) d * (double) d;
                }
                const float dv = (float) (sum / w);
                if (dv < bestD)
                {
                    bestD = dv;
                    bestL = L;
                }
            }
            if (bestL == 0)
                return coarse;

            // d at bestL+/-1 for a stride-1 parabola
            auto dAt = [&] (int L) noexcept -> float
            {
                double sum = 0.0;
                for (int i = 0; i < w; ++i)
                {
                    const float d = at (top + i) - at (top + i - L);
                    sum += (double) d * (double) d;
                }
                return (float) (sum / w);
            };
            const float dm = dAt (bestL - 1);
            const float dp = dAt (bestL + 1);
            const float denom = dm - 2.0f * bestD + dp;
            const float offset = denom > 1.0e-12f ? 0.5f * (dm - dp) / denom : 0.0f;
            return (float) bestL + juce::jlimit (-1.5f, 1.5f, offset);
        }
    };

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* semitones = nullptr;
    juce::AudioParameterFloat* mix = nullptr;

    std::array<GrainShifter, 2> shifters;
    PeriodTracker tracker;
    double currentSampleRate = 0.0;
};

} // namespace openguitarmultifx
