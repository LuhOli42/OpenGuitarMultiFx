#pragma once

#include "DeferredReclaimer.h"
#include "PitchDetector.h"
#include "PolyphonicPitchDetector.h"
#include "SignalGraph.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <memory>

namespace openguitarmultifx
{

/**
    Owns the audio device and the block loop. Implements
    juce::AudioIODeviceCallback directly -- no extra layers between the
    driver and the SignalGraph.

    See ARCHITECTURE.md section A/D: audioDeviceIOCallbackWithContext()
    runs on the audio thread and follows the rules that apply there;
    setSignalGraph() runs on the control thread and is the only way to
    change what's playing.
*/
class AudioEngine : private juce::AudioIODeviceCallback,
                     private juce::Timer
{
public:
    AudioEngine();
    ~AudioEngine() override;

    /** Control thread. Opens the default device and starts processing. */
    bool start();
    void stop();

    /** Control thread. The engine takes ownership of the graph. */
    void setSignalGraph (std::unique_ptr<SignalGraph> newGraph);

    /** Safe to read from any thread (it's just telemetry). */
    double getCurrentCpuUsage() const noexcept { return lastCpuUsage.load (std::memory_order_relaxed); }

    /** Safe to read from any thread. Peak level (0-1) of the selected input
        channel, and of the post-graph signal actually about to reach the
        device -- for FooterBar's IN/OUT meters. */
    float getInputLevel() const noexcept { return lastInputLevel.load (std::memory_order_relaxed); }
    float getOutputLevel() const noexcept { return lastOutputLevel.load (std::memory_order_relaxed); }

    /** Safe to read from any thread. 0 means no clear pitch detected
        (silence or noise) -- for FooterBar's tuner gauge. */
    float getDetectedFrequencyHz() const noexcept { return pitchDetector.getDetectedFrequencyHz(); }

    /** For the polyphonic tuner overlay -- see PolyphonicPitchDetector.h.
        Fed the exact same input tap as the mono pitchDetector above, just
        split into per-string bands. Control thread; safe from any thread. */
    void setTuningProfile (const TuningProfile& tuning) { polyphonicPitchDetector.setTuning (tuning); }
    int getTuningStringCount() const noexcept { return polyphonicPitchDetector.getNumStrings(); }
    PolyphonicPitchDetector::StringReading getTuningStringReading (int stringIndex) const noexcept
    {
        return polyphonicPitchDetector.getReading (stringIndex);
    }

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }

    // -- Input/output routing -----------------------------------------
    // Both are plain atomics read once per block on the audio thread and
    // written from the UI thread -- no graph rebuild needed, same idea as
    // an EffectProcessor's own parameters.

    /** Which input channel of the currently open device feeds the chain (clamped to what's available). */
    void setInputChannel (int channelIndex) noexcept { selectedInputChannel.store (channelIndex, std::memory_order_relaxed); }
    int getInputChannel() const noexcept { return selectedInputChannel.load (std::memory_order_relaxed); }

    /** One name per physical input channel the currently open device actually
        has -- read live from the device every call, never a fixed/assumed
        list, so a different interface (or none at all) never leaves a stale
        option on screen. */
    juce::StringArray getAvailableInputChannelNames() const;

    /** Index (0, 2, 4, ...) of the first channel of the selected OUTPUT PAIR.
        Everything outside [pair, pair+1] is silenced after the graph runs;
        every processor still always sees a full buffer to work with. This
        replaced an abstract "stereo/left/right" choice that didn't reflect
        the real device -- an interface with more than 2 outputs (e.g. a
        6-channel audio interface) can only be pointed at one of its real
        pairs, never an arbitrary/unbounded one. */
    void setOutputChannelPair (int pairStartIndex) noexcept { selectedOutputPairStart.store (pairStartIndex, std::memory_order_relaxed); }
    int getOutputChannelPair() const noexcept { return selectedOutputPairStart.load (std::memory_order_relaxed); }

    /** One entry per available output PAIR of the currently open device,
        e.g. {"Out 1/2", "Out 3/4", "Out 5/6"} for a 6-channel interface --
        always sized to what the live device reports, never a synthetic list. */
    juce::StringArray getAvailableOutputPairNames() const;

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                            float* const* outputChannelData, int numOutputChannels,
                                            int numSamples, const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    void timerCallback() override;

    juce::AudioDeviceManager deviceManager;
    DeferredReclaimer<SignalGraph> graphSlot;

    std::atomic<double> sampleRate { 0.0 };
    std::atomic<int> blockSize { 0 };
    std::atomic<double> lastCpuUsage { 0.0 };
    std::atomic<float> lastInputLevel { 0.0f };
    std::atomic<float> lastOutputLevel { 0.0f };
    PitchDetector pitchDetector;
    PolyphonicPitchDetector polyphonicPitchDetector;

    std::atomic<int> selectedInputChannel { 0 };
    std::atomic<int> selectedOutputPairStart { 0 };
};

} // namespace openguitarmultifx
