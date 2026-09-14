#include "AudioEngine.h"

namespace openguitarmultifx
{

namespace
{
    // A defensive ceiling, not a real one: on this dev machine, PipeWire
    // already holds the real audio interface open exclusively (confirmed
    // live -- see AGENT.md), so JUCE's ALSA backend can only reach it
    // through PipeWire's own generic ALSA passthrough device, which
    // advertises a placeholder 128-channel capability regardless of what
    // hardware is actually behind it (the interface used for that
    // confirmation only has 6 real channels each way). Actual audio
    // routing is unaffected either way (AudioEngine only ever opens 1
    // input / 2 outputs, see start()) -- this only stops the I/O selector
    // UI from listing channels nothing will ever really have. A real ALSA
    // target with no PipeWire in front of it (e.g. the final embedded
    // build) should never hit this ceiling in the first place.
    constexpr int maxSaneChannelCount = 16;
}

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    stop();
}

bool AudioEngine::start()
{
    const auto err = deviceManager.initialiseWithDefaultDevices (1, 2);

    if (err.isNotEmpty())
    {
        juce::Logger::writeToLog ("AudioEngine: failed to open device -- " + err);
        return false;
    }

    // JUCE's ALSA backend defaults to whatever the system calls "default",
    // which on a PipeWire desktop is PipeWire's own generic ALSA
    // passthrough ("Default ALSA Output", "Default Audio Device",
    // "PipeWire Sound Server") -- confirmed live (see AGENT.md) to report a
    // placeholder 128 input / 128 output channels on a machine whose real
    // interface only has 6 of each. That's exactly what made every I/O
    // selector look like it had "infinite" channels: it was listing that
    // placeholder device's channels, not the real interface's.
    //
    // Switch to whichever currently-connected device looks like a real USB
    // audio interface instead -- picked live every time this runs off
    // whatever's actually plugged in right now, never a name hardcoded for
    // one machine (there is no reliable "real interface" heuristic beyond
    // this; a proper device-picker in Settings is future work, not needed
    // while there's normally exactly one interface plugged in). This starts
    // from the setup that just successfully opened above and only redirects
    // which device it points to, rather than building a setup from scratch
    // -- passing a preferred device name straight into initialise() was
    // tried first and failed outright ("no channels") the one time it was
    // actually run against real hardware, presumably because the ALSA
    // backend's input/output device name lists aren't guaranteed to line up
    // the way that call assumes.
    if (auto* type = deviceManager.getCurrentDeviceTypeObject())
    {
        type->scanForDevices();

        juce::String usbInputName;
        for (auto& name : type->getDeviceNames (true))
        {
            if (name.containsIgnoreCase ("USB Audio"))
            {
                usbInputName = name;
                break;
            }
        }

        juce::String usbOutputName;
        if (usbInputName.isNotEmpty())
            for (auto& name : type->getDeviceNames (false))
                if (name == usbInputName)
                {
                    usbOutputName = name;
                    break;
                }

        juce::AudioDeviceManager::AudioDeviceSetup currentSetup;
        deviceManager.getAudioDeviceSetup (currentSetup);

        if (usbInputName.isNotEmpty() && usbOutputName.isNotEmpty()
            && (currentSetup.inputDeviceName != usbInputName || currentSetup.outputDeviceName != usbOutputName))
        {
            // A FRESH setup, not the placeholder device's -- its channel
            // bitmask/sample rate/buffer size are specific to a 128-channel
            // device and don't necessarily mean anything on a 6-channel
            // one. Leaving sampleRate/bufferSize/channel masks at their
            // just-default-constructed values (0 / empty / "use default")
            // is exactly what a fresh initialiseWithDefaultDevices() would
            // pick for this device, computed by JUCE itself rather than
            // carried over from a different device.
            juce::AudioDeviceManager::AudioDeviceSetup setup;
            setup.inputDeviceName = usbInputName;
            setup.outputDeviceName = usbOutputName;
            setup.useDefaultInputChannels = true;
            setup.useDefaultOutputChannels = true;

            const auto switchErr = deviceManager.setAudioDeviceSetup (setup, true);
            if (switchErr.isNotEmpty())
            {
                // A failed setAudioDeviceSetup() does NOT leave the manager
                // on the previous device -- confirmed live (see
                // Source/Engine/AGENTS.md): getCurrentAudioDevice() comes
                // back nullptr here, not the original placeholder default.
                // That silently broke every input-channel-dependent feature
                // (the I/O selectors falling back to a synthetic "Default"
                // entry, the tuner/level meters reading permanent silence)
                // whenever this switch failed, with no visible symptom
                // beyond a log line nobody was watching. Explicitly restore
                // the pre-switch setup instead of assuming JUCE did it.
                juce::Logger::writeToLog ("AudioEngine: could not switch to \"" + usbInputName + "\" -- "
                                           + switchErr + " -- restoring the previous device");

                const auto restoreErr = deviceManager.setAudioDeviceSetup (currentSetup, true);
                if (restoreErr.isNotEmpty())
                    juce::Logger::writeToLog ("AudioEngine: failed to restore the previous device too -- "
                                               + restoreErr);
            }
        }
    }

    if (auto* device = deviceManager.getCurrentAudioDevice())
        juce::Logger::writeToLog ("AudioEngine: using \"" + device->getName() + "\" -- "
                                   + juce::String (device->getInputChannelNames().size()) + " input(s), "
                                   + juce::String (device->getOutputChannelNames().size()) + " output(s)");

    deviceManager.addAudioCallback (this);
    startTimer (100); // DeferredReclaimer sweep at 10Hz -- comfortably above the 500ms safety margin
    return true;
}

void AudioEngine::stop()
{
    stopTimer();
    deviceManager.removeAudioCallback (this);
}

void AudioEngine::setSignalGraph (std::unique_ptr<SignalGraph> newGraph)
{
    if (newGraph != nullptr)
    {
        const auto sr = sampleRate.load (std::memory_order_relaxed);
        const auto bs = blockSize.load (std::memory_order_relaxed);
        if (sr > 0.0)
            newGraph->prepare (sr, bs, 2);
    }

    graphSlot.publish (std::move (newGraph));
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    sampleRate.store (device->getCurrentSampleRate(), std::memory_order_relaxed);
    blockSize.store (device->getCurrentBufferSizeSamples(), std::memory_order_relaxed);
    pitchDetector.prepare (device->getCurrentSampleRate());
    polyphonicPitchDetector.prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

void AudioEngine::audioDeviceStopped()
{
    sampleRate.store (0.0, std::memory_order_relaxed);
    blockSize.store (0, std::memory_order_relaxed);
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                                     float* const* outputChannelData, int numOutputChannels,
                                                     int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    const auto startTicks = juce::Time::getHighResolutionTicks();

    // An empty graph means total bypass: copy the selected input channel to
    // every output channel before anything else, and let the SignalGraph
    // process on top of that if there's anything in it. Output routing
    // (silencing L or R) happens AFTER the graph -- every processor still
    // always sees a full buffer to work with; routing is purely what
    // reaches the physical outputs.
    const int inChIndex = (numInputChannels > 0)
                               ? juce::jlimit (0, numInputChannels - 1, selectedInputChannel.load (std::memory_order_relaxed))
                               : 0;
    const float* in = (numInputChannels > 0) ? inputChannelData[inChIndex] : nullptr;

    if (in != nullptr)
    {
        pitchDetector.pushSamples (in, numSamples);
        polyphonicPitchDetector.pushSamples (in, numSamples);
        const auto range = juce::FloatVectorOperations::findMinAndMax (in, numSamples);
        lastInputLevel.store (juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd())),
                               std::memory_order_relaxed);
    }
    else
    {
        lastInputLevel.store (0.0f, std::memory_order_relaxed);
    }

    for (int ch = 0; ch < numOutputChannels; ++ch)
    {
        auto* out = outputChannelData[ch];

        if (in != nullptr)
            juce::FloatVectorOperations::copy (out, in, numSamples);
        else
            juce::FloatVectorOperations::clear (out, numSamples);
    }

    juce::AudioBuffer<float> buffer (outputChannelData, numOutputChannels, numSamples);

    if (auto* graph = graphSlot.currentRaw())
        graph->process (buffer);

    // Peak across every channel the graph actually produced -- what's
    // about to reach the device, before the pair-silencing below decides
    // which of those channels the listener actually hears.
    float outPeak = 0.0f;
    for (int ch = 0; ch < numOutputChannels; ++ch)
    {
        const auto range = juce::FloatVectorOperations::findMinAndMax (outputChannelData[ch], numSamples);
        outPeak = juce::jmax (outPeak, std::abs (range.getStart()), std::abs (range.getEnd()));
    }
    lastOutputLevel.store (outPeak, std::memory_order_relaxed);

    // Silence every physical output channel outside the selected pair --
    // clamped to what this device actually has, so a stale selection from a
    // previously-connected interface with more outputs can never reach past
    // the end of the current one's channel array.
    if (numOutputChannels > 0)
    {
        const int pairStart = juce::jlimit (0, numOutputChannels - 1,
                                             selectedOutputPairStart.load (std::memory_order_relaxed));
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (ch < pairStart || ch > pairStart + 1)
                juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
    }

    const auto elapsedSeconds = juce::Time::highResolutionTicksToSeconds (
        juce::Time::getHighResolutionTicks() - startTicks);
    const auto sr = sampleRate.load (std::memory_order_relaxed);
    const auto blockSeconds = numSamples / (sr > 0.0 ? sr : 48000.0);

    lastCpuUsage.store (blockSeconds > 0.0 ? elapsedSeconds / blockSeconds : 0.0,
                         std::memory_order_relaxed);
}

void AudioEngine::timerCallback()
{
    graphSlot.sweep();
}

juce::StringArray AudioEngine::getAvailableInputChannelNames() const
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return {};

    auto names = device->getInputChannelNames();
    if (names.size() > maxSaneChannelCount)
        names.removeRange (maxSaneChannelCount, names.size() - maxSaneChannelCount);
    return names;
}

juce::StringArray AudioEngine::getAvailableOutputPairNames() const
{
    juce::StringArray pairs;
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return pairs;

    const auto count = juce::jmin (maxSaneChannelCount, device->getOutputChannelNames().size());
    for (int i = 0; i < count; i += 2)
    {
        pairs.add (i + 1 < count ? "Out " + juce::String (i + 1) + "/" + juce::String (i + 2)
                                  : "Out " + juce::String (i + 1));
    }
    return pairs;
}

} // namespace openguitarmultifx
