#include "PitchDetector.h"

#include <cmath>

namespace openguitarmultifx
{

void PitchDetector::prepare (double sampleRateToUse, float minFrequencyHz, float maxFrequencyHz, float silenceThreshold)
{
    sampleRate = sampleRateToUse;
    minFreqHz = minFrequencyHz;
    maxFreqHz = maxFrequencyHz;
    silenceRmsThreshold = silenceThreshold;

    tauMax = (int) (sampleRate / (double) minFreqHz) + 32;
    tauMin = juce::jmax (1, (int) (sampleRate / (double) maxFreqHz));
    windowSize = tauMax * 2;
    ringSize = juce::nextPowerOfTwo (windowSize * 2 + 4096);

    ringBuffer.assign ((size_t) ringSize, 0.0f);
    analysisBuffer.assign ((size_t) windowSize, 0.0f);
    diffBuffer.assign ((size_t) tauMax + 1, 0.0f);
    cmndBuffer.assign ((size_t) tauMax + 1, 0.0f);

    writePos.store (0, std::memory_order_relaxed);
    detectedFrequencyHz.store (0.0f, std::memory_order_relaxed);
}

void PitchDetector::pushSamples (const float* data, int numSamples) noexcept
{
    if (ringSize == 0)
        return;

    const int mask = ringSize - 1;
    int pos = writePos.load (std::memory_order_relaxed);
    for (int i = 0; i < numSamples; ++i)
    {
        ringBuffer[(size_t) pos] = data[i];
        pos = (pos + 1) & mask;
    }
    writePos.store (pos, std::memory_order_release);
}

void PitchDetector::analyse() noexcept
{
    if (ringSize == 0)
        return;
    runAnalysis();
}

void PitchDetector::runAnalysis() noexcept
{
    const int mask = ringSize - 1;

    // Copy the most recent windowSize samples out in order (oldest to
    // newest) -- O(windowSize), negligible next to the O(windowSize *
    // tauMax) analysis below, and it keeps that analysis's inner loops
    // simple linear array indexing instead of wraparound math.
    int readPos = (writePos.load (std::memory_order_acquire) - windowSize) & mask;
    for (int i = 0; i < windowSize; ++i)
    {
        analysisBuffer[(size_t) i] = ringBuffer[(size_t) readPos];
        readPos = (readPos + 1) & mask;
    }

    float rms = 0.0f;
    for (float s : analysisBuffer)
        rms += s * s;
    rms = std::sqrt (rms / (float) windowSize);

    // Hysteresis: a lower bar to keep a note than to pick one up -- see releaseThresholdRatio.
    if (rms < (wasTracking ? silenceRmsThreshold * releaseThresholdRatio : silenceRmsThreshold))
    {
        wasTracking = false;
        detectedFrequencyHz.store (0.0f, std::memory_order_relaxed);
        return;
    }

    // YIN difference function: d(tau) = sum (x[j] - x[j+tau])^2.
    diffBuffer[0] = 0.0f;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        float sum = 0.0f;
        const int limit = windowSize - tau;
        for (int j = 0; j < limit; ++j)
        {
            const float d = analysisBuffer[(size_t) j] - analysisBuffer[(size_t) (j + tau)];
            sum += d * d;
        }
        diffBuffer[(size_t) tau] = sum;
    }

    // Cumulative mean normalized difference function.
    cmndBuffer[0] = 1.0f;
    float runningSum = 0.0f;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        runningSum += diffBuffer[(size_t) tau];
        cmndBuffer[(size_t) tau] = runningSum > 0.0f ? diffBuffer[(size_t) tau] * (float) tau / runningSum : 1.0f;
    }

    // First dip below the absolute threshold that's also a local minimum
    // -- the standard YIN period estimate. Starting the search at tauMin
    // skips lags corresponding to frequencies above maxFreqHz, which
    // would otherwise be indistinguishable from (and often lower-error
    // than) the true fundamental for a bright/overtone-rich signal.
    int bestTau = -1;
    for (int tau = tauMin; tau < tauMax; ++tau)
    {
        if (cmndBuffer[(size_t) tau] < yinThreshold && cmndBuffer[(size_t) tau] < cmndBuffer[(size_t) (tau + 1)])
        {
            bestTau = tau;
            break;
        }
    }

    if (bestTau < 0)
    {
        wasTracking = false;
        detectedFrequencyHz.store (0.0f, std::memory_order_relaxed);
        return;
    }

    // Parabolic interpolation across the minimum for sub-sample accuracy
    // -- without this, pitch estimates are quantised to whole-sample lag
    // steps, which at guitar frequencies is audibly/visibly imprecise
    // (multiple cents of error near the low end of the range).
    auto interpolate = [this] (int tau, const std::vector<float>& curve)
    {
        float refined = (float) tau;
        if (tau > 0 && tau < tauMax)
        {
            const float s0 = curve[(size_t) (tau - 1)];
            const float s1 = curve[(size_t) tau];
            const float s2 = curve[(size_t) (tau + 1)];
            const float denom = 2.0f * (2.0f * s1 - s0 - s2);
            if (std::abs (denom) > 1.0e-9f)
                refined += (s0 - s2) / denom;
        }
        return refined;
    };

    const float betterTau = interpolate (bestTau, cmndBuffer);

    // Long-baseline refinement. One period's lag is only a handful of samples for a high note (E6 is 36), so whatever
    // fraction of a sample the interpolation above is off by is a big fraction of the PERIOD -- measured 9 cents at E4
    // and 38 cents at E6, and it was that far out before the range change too. The signal repeats at every MULTIPLE of
    // the period, so measuring a large multiple and dividing costs m times fewer cents for the same lag error.
    //
    // Done by DOUBLING the multiple, not by jumping straight to the largest that fits: the search has to look near
    // m x (current estimate), and if that estimate is off by e the target is off by m x e -- one big jump lands outside
    // any safe search window and finds the wrong period multiple (it did: B3 and E6 stayed wrong). Each doubling roughly
    // halves the relative error, so a window of a quarter period around the new centre always contains the true one.
    // Costs a few interpolations and no new analysis: diffBuffer already holds every lag.
    double period = (double) betterTau;
    for (int multiple = 2; period * multiple + 1.0 < (double) (tauMax - 1); multiple *= 2)
    {
        const int centre = (int) std::lround (period * (double) multiple);
        const int search = juce::jmax (2, (int) (period * 0.25));
        int best = -1;
        float bestValue = 0.0f;
        for (int tau = juce::jmax (1, centre - search); tau <= juce::jmin (tauMax - 1, centre + search); ++tau)
            if (best < 0 || diffBuffer[(size_t) tau] < bestValue)
            {
                best = tau;
                bestValue = diffBuffer[(size_t) tau];
            }

        if (best <= 0)
            break;

        const double refined = (double) interpolate (best, diffBuffer) / (double) multiple;
        // Only accept a refinement that agrees with what we already had -- otherwise a neighbouring period multiple
        // would be reported with great confidence.
        if (std::abs (refined - period) > period * 0.1)
            break;
        period = refined;
    }

    wasTracking = true;
    detectedFrequencyHz.store ((float) (sampleRate / period), std::memory_order_relaxed);
}

} // namespace openguitarmultifx
