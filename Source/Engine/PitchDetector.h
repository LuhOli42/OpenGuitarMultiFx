#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <vector>

namespace openguitarmultifx
{

/**
    A YIN-algorithm pitch detector for the tuner (`FooterBar`'s tuner
    gauge, wired through `AudioEngine::getDetectedFrequencyHz()`).

    Realtime-safe by construction: every buffer is sized once in
    prepare() (never reallocated from pushSamples()), and the O(window *
    maxLag) YIN analysis itself only runs once every ~1/15th of a second
    of new audio (a tuner doesn't need to update faster than that, and
    running the full analysis every single callback would be needless
    CPU burn for no perceptible benefit) -- pushSamples() just accumulates
    into a ring buffer the rest of the time.

    YIN (de Cheveigné & Kawahara, 2002) rather than plain autocorrelation:
    plain autocorrelation on a guitar's harmonically-rich signal easily
    locks onto an overtone instead of the fundamental (a classic "octave
    error"); YIN's cumulative-mean-normalized difference function is
    specifically the standard fix for that failure mode.
*/
class PitchDetector
{
public:
    /** Control thread (called from AudioEngine::audioDeviceAboutToStart()).
        Defaults match this class's original fixed range/threshold (the
        mono tuner's exact prior behaviour, unchanged). PolyphonicPitchDetector
        passes tighter, band-specific values instead: a shared 70-1200Hz
        range can't even find several extended-range/bass strings (B0/E1/
        F#1/A1/B1 all fall under 70Hz), and a fixed silence threshold tuned
        for a full-band signal reads a bandpass-filtered band's much
        quieter signal as silence far too often. */
    void prepare (double sampleRateToUse, float minFrequencyHz = 70.0f, float maxFrequencyHz = 1200.0f,
                  float silenceThreshold = 0.01f);

    /** Audio thread. Never allocates -- all buffers are sized in prepare(). */
    void pushSamples (const float* data, int numSamples) noexcept;

    /** Safe to read from any thread. 0 means "no clear pitch" (silence or noise). */
    float getDetectedFrequencyHz() const noexcept { return detectedFrequencyHz.load (std::memory_order_relaxed); }

private:
    void runAnalysis() noexcept;

    static constexpr float yinThreshold = 0.15f; // standard YIN absolute threshold
    static constexpr double updateRateHz = 15.0; // plenty responsive for a tuner display

    float minFreqHz = 70.0f;
    float maxFreqHz = 1200.0f;
    float silenceRmsThreshold = 0.01f;

    std::vector<float> ringBuffer, analysisBuffer, diffBuffer, cmndBuffer;
    int ringSize = 0;
    int windowSize = 0;
    int tauMin = 0;
    int tauMax = 0;
    int writePos = 0;
    int samplesSinceAnalysis = 0;
    int analysisIntervalSamples = 0;
    double sampleRate = 0.0;

    std::atomic<float> detectedFrequencyHz { 0.0f };
};

} // namespace openguitarmultifx
