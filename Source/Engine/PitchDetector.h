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

        The default range covers **every string of every instrument this app tunes**, down to a 5-string bass's B0
        (30.87 Hz): the old 70 Hz floor could not find B0, E1, F#1, A1 or B1 at all -- the lag the search reaches never
        gets that long -- which is why low notes did not register (user report, 2026-09-21). It is affordable now only
        because the analysis runs on the control thread; see analyse(). */
    void prepare (double sampleRateToUse, float minFrequencyHz = 28.0f, float maxFrequencyHz = 1400.0f,
                  float silenceThreshold = 0.002f);

    /** Audio thread. Writes the samples into the ring buffer and nothing else -- O(numSamples), no analysis.

        YIN's own pass is O(window x tauMax), which at a 28 Hz floor is ~6 million operations: it used to run from here,
        landing entirely inside ONE audio block (measured 0.7 ms of a 2.67 ms budget at the old 70 Hz floor, ~4 ms at
        this one) whenever the periodic trigger came round. That is unbounded work on the audio thread; it belongs on the
        control thread, which is what analyse() is for. */
    void pushSamples (const float* data, int numSamples) noexcept;

    /** Control thread, at whatever rate the UI polls (the app's 20 Hz timer). Runs the analysis over the most recent
        window of what pushSamples() has written. Safe against the audio thread without a lock: the ring holds several
        windows plus 4096 samples of slack (85 ms at 48 kHz) and this reads BEHIND the write position, so the producer
        cannot lap the reader between two calls -- the same generous-margin reasoning as DeferredReclaimer's sweep. */
    void analyse() noexcept;

    /** Safe to read from any thread. 0 means "no clear pitch" (silence or noise). */
    float getDetectedFrequencyHz() const noexcept { return detectedFrequencyHz.load (std::memory_order_relaxed); }

private:
    void runAnalysis() noexcept;

    static constexpr float yinThreshold = 0.15f; // standard YIN absolute threshold

    /** Once a note IS being tracked, keep tracking it this much further down before calling it silence. A plucked note
        decays continuously, so a single level gate makes the display drop the note while it is still clearly audible
        (user report 2026-09-22, "se o som diminuir ele para de captar mesmo ainda tendo som"); every hardware tuner
        holds on the way down instead. YIN's own confidence test still has to pass, so this cannot invent a note. */
    static constexpr float releaseThresholdRatio = 0.25f;

    float minFreqHz = 70.0f;
    float maxFreqHz = 1200.0f;
    float silenceRmsThreshold = 0.01f;

    std::vector<float> ringBuffer, analysisBuffer, diffBuffer, cmndBuffer;
    int ringSize = 0;
    int windowSize = 0;
    int tauMin = 0;
    int tauMax = 0;
    std::atomic<int> writePos { 0 }; // written by the audio thread, read by analyse() on the control thread
    double sampleRate = 0.0;

    std::atomic<float> detectedFrequencyHz { 0.0f };
    bool wasTracking = false; // control thread only: which side of the hysteresis we are on
};

} // namespace openguitarmultifx
