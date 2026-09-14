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

    writePos = 0;
    samplesSinceAnalysis = 0;
    analysisIntervalSamples = juce::jmax (1, (int) (sampleRate / updateRateHz));
    detectedFrequencyHz.store (0.0f, std::memory_order_relaxed);
}

void PitchDetector::pushSamples (const float* data, int numSamples) noexcept
{
    if (ringSize == 0)
        return;

    const int mask = ringSize - 1;
    for (int i = 0; i < numSamples; ++i)
    {
        ringBuffer[(size_t) writePos] = data[i];
        writePos = (writePos + 1) & mask;
    }

    samplesSinceAnalysis += numSamples;
    if (samplesSinceAnalysis >= analysisIntervalSamples)
    {
        samplesSinceAnalysis = 0;
        runAnalysis();
    }
}

void PitchDetector::runAnalysis() noexcept
{
    const int mask = ringSize - 1;

    // Copy the most recent windowSize samples out in order (oldest to
    // newest) -- O(windowSize), negligible next to the O(windowSize *
    // tauMax) analysis below, and it keeps that analysis's inner loops
    // simple linear array indexing instead of wraparound math.
    int readPos = (writePos - windowSize) & mask;
    for (int i = 0; i < windowSize; ++i)
    {
        analysisBuffer[(size_t) i] = ringBuffer[(size_t) readPos];
        readPos = (readPos + 1) & mask;
    }

    float rms = 0.0f;
    for (float s : analysisBuffer)
        rms += s * s;
    rms = std::sqrt (rms / (float) windowSize);

    if (rms < silenceRmsThreshold)
    {
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
        detectedFrequencyHz.store (0.0f, std::memory_order_relaxed);
        return;
    }

    // Parabolic interpolation across the minimum for sub-sample accuracy
    // -- without this, pitch estimates are quantised to whole-sample lag
    // steps, which at guitar frequencies is audibly/visibly imprecise
    // (multiple cents of error near the low end of the range).
    float betterTau = (float) bestTau;
    if (bestTau > 0 && bestTau < tauMax)
    {
        const float s0 = cmndBuffer[(size_t) (bestTau - 1)];
        const float s1 = cmndBuffer[(size_t) bestTau];
        const float s2 = cmndBuffer[(size_t) (bestTau + 1)];
        const float denom = 2.0f * (2.0f * s1 - s0 - s2);
        if (std::abs (denom) > 1.0e-9f)
            betterTau += (s0 - s2) / denom;
    }

    detectedFrequencyHz.store ((float) (sampleRate / (double) betterTau), std::memory_order_relaxed);
}

} // namespace openguitarmultifx
