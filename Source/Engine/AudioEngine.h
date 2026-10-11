#pragma once

#include "DeferredReclaimer.h"
#include "MasterEq.h"
#include "PitchDetector.h"
#include "SignalGraph.h"
#include "Spillover.h"

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

    /** Control thread. Hands an outgoing preset's trail-capable processors
        to the engine as a spill unit: they keep processing on silence in
        parallel with the new chain until their tails die (preset
        spillover -- see Spillover.h and docs/PresetsAndSnapshots.md).
        Prepared for the current stream here; a no-op while no device is
        running. */
    void addSpillover (std::unique_ptr<Spillover::Unit> unit);

    /** Safe to read from any thread (it's just telemetry). The AVERAGE share of the block budget over the last ~0.5 s --
        "how loaded am I". */
    double getCurrentCpuUsage() const noexcept { return averageCpuUsage.load (std::memory_order_relaxed); }

    /** The WORST block of the last ~1 s. This is the number that decides whether audio drops out (one block over 100% is
        a dropout) and it is normally well above the average -- showing only this one reads as "the CPU is jumping around"
        when nothing is wrong, and showing only the average hides a real dropout. The UI shows both. */
    double getPeakCpuUsage() const noexcept { return peakCpuUsage.load (std::memory_order_relaxed); }

    /** Safe to read from any thread. Peak level (0-1) of the selected input
        channel, and of the post-graph signal actually about to reach the
        device -- for FooterBar's IN/OUT meters. */
    float getInputLevel() const noexcept { return lastInputLevel.load (std::memory_order_relaxed); }
    float getOutputLevel() const noexcept { return lastOutputLevel.load (std::memory_order_relaxed); }

    /** Safe to read from any thread. 0 means no clear pitch detected
        (silence or noise) -- for FooterBar's tuner gauge. */
    float getDetectedFrequencyHz() const noexcept { return pitchDetector.getDetectedFrequencyHz(); }

    /** Input sensitivity, in dB, applied to the signal before the chain (and reflected in the IN meter). This is the
        gain-staging knob between an interface and the models: every circuit here reads a sample of 1.0 as one VOLT at its
        input jack, because that is what the schematics are in. A guitar gives roughly 0.05-0.3 V, so an interface whose
        own gain lands the guitar near full scale drives every model several times harder than the real pedal or amp would
        ever see -- which is heard as "it is always saturated, playing softer does not clean up". Control thread. */
    void setInputGainDb (float db) noexcept
    {
        inputGainDb.store (db, std::memory_order_relaxed);
        inputGainLinear.store (juce::Decibels::decibelsToGain (db), std::memory_order_relaxed);
    }
    float getInputGainDb() const noexcept { return inputGainDb.load (std::memory_order_relaxed); }

    /** Control thread. Turns the tuner's pitch analysis on and off. It is OFF by default and only runs while the tuner is
        on screen: YIN's full pass is O(window x maxLag) and fires periodically, so it lands entirely in ONE audio block --
        measured 0.71 ms (mono) + 2.28 ms (6 strings) in a single 2.67 ms block, i.e. over budget on its own, before any
        effect runs, and independent of what is in the chain. That was the "CPU 50-80% no matter how many pedals" the user
        reported on 2026-09-21. */
    void setTunerActive (bool shouldBeActive) noexcept { tunerActive.store (shouldBeActive, std::memory_order_relaxed); }
    bool isTunerActive() const noexcept { return tunerActive.load (std::memory_order_relaxed); }

    /** Master output gain, dB (-60 .. +12, below -60 = mute), applied AFTER the whole graph and the master EQ,
        before the OUT meter and the device -- the FooterBar's master volume knob. The audio thread ramps to the
        target over ~20 ms (SmoothedValue), so dragging the knob can't zipper. Control thread. */
    void setMasterGainDb (float db) noexcept
    {
        masterGainDb.store (juce::jlimit (-70.0f, 12.0f, db), std::memory_order_relaxed);
        masterGainLinear.store (juce::Decibels::decibelsToGain (masterGainDb.load (std::memory_order_relaxed)),
                                std::memory_order_relaxed);
    }
    float getMasterGainDb() const noexcept { return masterGainDb.load (std::memory_order_relaxed); }

    /** Control thread. Replaces the master-bus EQ band list (see MasterEq.h -- free-form parametric bands
        edited from the footer's EQ card). Coefficients are rebuilt here and published to the audio thread
        through the same DeferredReclaimer primitive the graph uses; persisted app-level by the caller
        (MasterBusSettings.h), never inside a preset. */
    void setMasterEqBands (const std::vector<MasterEqBand>& bands) { masterEq.setBands (bands); }
    const std::vector<MasterEqBand>& getMasterEqBands() const noexcept { return masterEq.getBands(); }

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }

    /** Sample rates the OPEN device supports (empty when no device is open) and the one it is running at (0 when stopped). Control thread. */
    juce::Array<double> getAvailableSampleRates() const;
    double getCurrentSampleRate() const noexcept { return sampleRate.load (std::memory_order_relaxed); }

    /** Control thread. Restarts the device at `newRate` (audio is interrupted for a moment), re-prepares the running graph for it, and
        remembers the choice for the next launch. Returns an empty string on success, otherwise why it failed (the previous rate is restored). */
    juce::String setSampleRate (double newRate);

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

    /** Audio thread. Runs the current graph plus any ringing preset
        spillovers on top of it. */
    void processGraph (juce::AudioBuffer<float>& buffer, int numSamples);

    /** Audio thread. The graph itself: when a new one has just been published (a block was moved, added, removed, a
        row re-routed) the retired graph and the new one run on the same input for ~10 ms and are mixed with an
        equal-power crossfade -- the outgoing lane keeps driving sound while the new one takes over, so there is no
        gap and no step between the two unrelated outputs. */
    void processCurrentGraph (juce::AudioBuffer<float>& buffer, int numSamples);

    juce::AudioDeviceManager deviceManager;
    DeferredReclaimer<SignalGraph> graphSlot;
    Spillover spillover;
    MasterEq masterEq;

    std::atomic<double> sampleRate { 0.0 };
    std::atomic<int> blockSize { 0 };
    std::atomic<double> averageCpuUsage { 0.0 }, peakCpuUsage { 0.0 };
    std::atomic<float> lastInputLevel { 0.0f };
    std::atomic<float> lastOutputLevel { 0.0f };
    PitchDetector pitchDetector;

    // Audio thread only (reset in audioDeviceAboutToStart, before the callback runs): the graph swap's crossfade.
    SignalGraph* activeGraph = nullptr;      // the graph the previous block ran
    SignalGraph* fadingOutGraph = nullptr;   // a retired graph still playing its crossfade leg (kept alive by graphSlot's margin)
    int fadeOutSamplesLeft = 0, fadeOutSamplesTotal = 0;
    juce::int64 lastCallbackTicks = 0;
    // Holds the new graph's output during a crossfade -- sized in audioDeviceAboutToStart (control thread),
    // never touched for allocation on the audio thread.
    juce::AudioBuffer<float> crossfadeScratch;

    std::atomic<bool> tunerActive { false }; // see setTunerActive()
    std::atomic<float> inputGainDb { 0.0f }, inputGainLinear { 1.0f }; // see setInputGainDb()
    std::atomic<float> masterGainDb { 0.0f }, masterGainLinear { 1.0f }; // see setMasterGainDb()
    // Audio thread only: ramps masterGainLinear's target over ~20 ms so a footer-knob drag can't zipper.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> masterGainSmooth;
    std::atomic<int> selectedInputChannel { 0 };
    std::atomic<int> selectedOutputPairStart { 0 };
};

} // namespace openguitarmultifx
