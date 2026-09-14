#pragma once

#include "DeferredReclaimer.h"
#include "PitchDetector.h"
#include "TuningProfile.h"

#include <juce_dsp/juce_dsp.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <memory>
#include <vector>

namespace openguitarmultifx
{

/**
    Per-string pitch estimation for the polyphonic tuner overlay -- NOT
    general polyphonic transcription (unreliable, heavy for what a tuner
    needs). Same practical technique real polyphonic tuner pedals use:
    split the input into one bandpass filter per string of the active
    TuningProfile (4-8 bands), then run the same YIN PitchDetector already
    trusted for the mono tuner independently on each filtered band. A
    band's own filter only has to reject its NEIGHBOURS well enough for
    that band's YIN pass to lock onto the right string, not perfectly
    isolate it.

    Realtime-safety follows the exact NAMProcessor/IRLoaderProcessor
    pattern: changing the tuning (setTuning(), control thread) builds an
    entirely fresh set of bands off to the side and swaps it in atomically
    via DeferredReclaimer -- never mutates the live bands while the audio
    thread might be inside pushSamples() on them.
*/
class PolyphonicPitchDetector : private juce::Timer
{
public:
    PolyphonicPitchDetector();
    ~PolyphonicPitchDetector() override;

    /** Control thread. */
    void prepare (double sampleRateToUse, int maxBlockSize);
    /** Control thread. Safe to call any time, including before prepare()
        (applied once prepare() knows the sample rate). */
    void setTuning (const TuningProfile& tuning);

    /** Audio thread. Never allocates -- bandScratch is sized once in
        prepare(). No-op if setTuning() hasn't published a band set yet. */
    void pushSamples (const float* data, int numSamples) noexcept;

    struct StringReading
    {
        bool active = false;
        float frequencyHz = 0.0f;
        float cents = 0.0f; // -50..+50, only meaningful when active
        juce::String noteName;
    };

    /** Safe to read from any thread. */
    int getNumStrings() const noexcept;
    /** Safe to read from any thread. Default-constructed StringReading if
        stringIndex is out of range for the currently active tuning. */
    StringReading getReading (int stringIndex) const noexcept;

private:
    struct Band
    {
        juce::dsp::IIR::Filter<float> filter;
        PitchDetector pitchDetector;
        float targetFrequencyHz = 0.0f;
        juce::String noteName;
        std::atomic<bool> active { false };
        std::atomic<float> frequencyHz { 0.0f };
        std::atomic<float> cents { 0.0f };
    };

    struct BandSet
    {
        std::vector<std::unique_ptr<Band>> bands;
    };

    void timerCallback() override { bandSetSlot.sweep(); } // see DeferredReclaimer

    DeferredReclaimer<BandSet> bandSetSlot;
    std::vector<float> bandScratch;

    double sampleRate = 0.0;
    int preparedBlockSize = 0;
    TuningProfile pendingTuning; // applied once prepare() has a real sample rate
};

} // namespace openguitarmultifx
