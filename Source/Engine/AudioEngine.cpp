#include "AudioEngine.h"
#include "SoftCeiling.h"

#include <cmath>
#if JUCE_LINUX
 #include <sched.h> // sched_getscheduler, for the native-callback RT check in detectCallbackRealtime
#endif

namespace openguitarmultifx
{

namespace
{
    juce::File sampleRateFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("OpenGuitarMultiFx").getChildFile ("sample_rate.txt");
    }

    double loadSampleRatePreference()
    {
        const auto f = sampleRateFile();
        return f.existsAsFile() ? f.loadFileAsString().trim().getDoubleValue() : 0.0;
    }

    void saveSampleRatePreference (double rate)
    {
        sampleRateFile().getParentDirectory().createDirectory();
        sampleRateFile().replaceWithText (juce::String (rate, 0));
    }

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
    spillover.clear(); // the callback is off now: drop every unit on the control thread
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

    // The rate: the user's saved choice, otherwise 48 kHz (the project's default and what an interface / PipeWire normally runs at) when the
    // device offers it -- JUCE's own default picked 44.1 kHz on the UR44, which then resampled against a 48 kHz graph.
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const auto wanted = loadSampleRatePreference();
        const auto rates = device->getAvailableSampleRates();
        const double target = wanted > 0.0 ? wanted : 48000.0;
        if (rates.contains (target) && std::abs (device->getCurrentSampleRate() - target) > 0.5)
        {
            juce::AudioDeviceManager::AudioDeviceSetup setup;
            deviceManager.getAudioDeviceSetup (setup);
            setup.sampleRate = target;
            const auto err = deviceManager.setAudioDeviceSetup (setup, true);
            if (err.isNotEmpty())
                juce::Logger::writeToLog ("AudioEngine: could not open the device at " + juce::String (target) + " Hz -- " + err);
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

juce::Array<double> AudioEngine::getAvailableSampleRates() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getAvailableSampleRates();
    return {};
}

juce::String AudioEngine::setSampleRate (double newRate)
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return "no audio device is open";
    if (std::abs (device->getCurrentSampleRate() - newRate) < 0.5)
    {
        saveSampleRatePreference (newRate);
        return {};
    }

    juce::AudioDeviceManager::AudioDeviceSetup previous, setup;
    deviceManager.getAudioDeviceSetup (previous);
    setup = previous;
    setup.sampleRate = newRate;

    // Take the callback off first: the running graph's processors are re-prepared for the new rate in audioDeviceAboutToStart, which must
    // not overlap a block being processed with the old state.
    deviceManager.removeAudioCallback (this);
    auto err = deviceManager.setAudioDeviceSetup (setup, true);
    if (err.isNotEmpty())
    {
        juce::Logger::writeToLog ("AudioEngine: could not switch to " + juce::String (newRate) + " Hz -- " + err + " -- restoring the previous rate");
        const auto restoreErr = deviceManager.setAudioDeviceSetup (previous, true);
        if (restoreErr.isNotEmpty())
            juce::Logger::writeToLog ("AudioEngine: failed to restore the previous rate too -- " + restoreErr);
    }
    else
    {
        saveSampleRatePreference (newRate);
    }
    deviceManager.addAudioCallback (this);
    return err;
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
        newGraph->setAudioCallerIsRealtime (callbackIsRealtime.load (std::memory_order_relaxed));
        const auto sr = sampleRate.load (std::memory_order_relaxed);
        const auto bs = blockSize.load (std::memory_order_relaxed);
        if (sr > 0.0)
            newGraph->prepare (sr, bs, 2);
    }

    graphSlot.publish (std::move (newGraph));
}

void AudioEngine::addSpillover (std::unique_ptr<Spillover::Unit> unit)
{
    if (unit == nullptr || unit->graph == nullptr)
        return;

    const auto sr = sampleRate.load (std::memory_order_relaxed);
    const auto bs = blockSize.load (std::memory_order_relaxed);
    if (sr <= 0.0 || bs <= 0)
        return; // no device running -- nothing to spill onto

    unit->graph->setAudioCallerIsRealtime (callbackIsRealtime.load (std::memory_order_relaxed));
    unit->prepare (sr, bs, 2); // prepareIfNeeded keeps every processor's tail state
    spillover.add (std::move (unit));
}

void AudioEngine::processGraph (juce::AudioBuffer<float>& buffer, int numSamples)
{
    processCurrentGraph (buffer, numSamples);

    // Spills never run over samples a swap fade covered: the recorded tail already carries the
    // outgoing lane's sound in that region, and a pending spill adding its tail on top would
    // double-count it. When the fade ended MID-block the spill still belongs in the remainder
    // the fade didn't touch -- run it there through a non-owning buffer view (fixed pointer
    // array, no allocation on the audio thread).
    if (! swapFadeRanThisBlock)
    {
        spillover.process (buffer, sampleRate.load (std::memory_order_relaxed));
    }
    else if (swapFadeSamplesThisBlock < numSamples)
    {
        const int chs = juce::jmin ((int) std::size (spillSlicePtrs), buffer.getNumChannels());
        for (int ch = 0; ch < chs; ++ch)
            spillSlicePtrs[ch] = buffer.getWritePointer (ch) + swapFadeSamplesThisBlock;
        juce::AudioBuffer<float> remainder (spillSlicePtrs, chs, numSamples - swapFadeSamplesThisBlock);
        spillover.process (remainder, sampleRate.load (std::memory_order_relaxed));
    }
}

void AudioEngine::processCurrentGraph (juce::AudioBuffer<float>& buffer, int numSamples)
{
    auto* graph = graphSlot.currentRaw();
    const auto now = juce::Time::getHighResolutionTicks();

    if (graph != activeGraph)
    {
        // Arm the swap fade: replay the last ~10 ms of output the outgoing graph already produced
        // (recorded in outputTailRing) faded out under the new graph's live output, played BACKWARD
        // from the most recent sample -- the first faded sample IS the last sample emitted, so the
        // boundary is continuous even for identical graphs; replaying the window forward would jump
        // the waveform ~10 ms back in time and click. The retired graph itself is never run --
        // every processor it shares with the new graph would advance its state twice per block
        // otherwise, which is exactly the rule spills live by.
        const double rate = sampleRate.load (std::memory_order_relaxed);
        const int window = rate > 0.0 ? juce::jmax (numSamples, (int) (0.010 * rate)) : 0;
        const bool recent = activeGraph != nullptr
                            && juce::Time::highResolutionTicksToSeconds (now - lastCallbackTicks) < 0.1;
        if (recent && ! swapFadeActive && window > 0 && window <= outputTailRing.getNumSamples()
            && tailRingSamples >= window && outputTailRing.getNumChannels() >= buffer.getNumChannels())
        {
            swapFadeActive = true;
            fadeOutSamplesTotal = fadeOutSamplesLeft = window;
            tailReadIndex = (tailRingWrite - 1 + outputTailRing.getNumSamples())
                            % outputTailRing.getNumSamples();
        }
        activeGraph = graph;
    }
    lastCallbackTicks = now;
    swapFadeRanThisBlock = false;
    swapFadeSamplesThisBlock = 0;

    if (graph != nullptr)
        graph->process (buffer);

    // Record this block's output before any fade mixing: the ring always holds the clean
    // single-graph signal a later swap replays. Frozen while a fade is active: when the ring is
    // exactly one window long the writer would clobber the tail before the replay reaches it.
    const int ringLen = outputTailRing.getNumSamples();
    if (ringLen > 0 && ! swapFadeActive)
    {
        const int ringChans = outputTailRing.getNumChannels();
        const int w0 = tailRingWrite;
        for (int ch = 0; ch < buffer.getNumChannels() && ch < ringChans; ++ch)
        {
            auto* w = outputTailRing.getWritePointer (ch);
            const auto* s = buffer.getReadPointer (ch);
            int p = w0;
            for (int i = 0; i < numSamples; ++i)
            {
                w[p] = s[i];
                if (++p >= ringLen) p = 0;
            }
        }
        tailRingWrite = (w0 + numSamples) % ringLen;
        tailRingSamples = juce::jmin (ringLen, tailRingSamples + numSamples);
    }

    if (swapFadeActive)
    {
        // out = live*t + tail*(1-t), linear law: the legs are usually the same processors under a
        // new routing plus the recorded continuation of the same signal (correlated), where an
        // equal-power pair would surge ~3 dB at the midpoint; here linear stays level and cannot
        // push an unchanged signal past unity.
        const int n = juce::jmin (numSamples, fadeOutSamplesLeft);
        const double dt = 1.0 / (double) fadeOutSamplesTotal;
        const double t0 = 1.0 - (double) fadeOutSamplesLeft / (double) fadeOutSamplesTotal;
        for (int ch = 0; ch < buffer.getNumChannels() && ch < outputTailRing.getNumChannels(); ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            const auto* tail = outputTailRing.getReadPointer (ch);
            double t = t0;
            int p = tailReadIndex;
            for (int i = 0; i < n; ++i, t += dt)
            {
                d[i] = (float) ((double) d[i] * t + (double) tail[p] * (1.0 - t));
                if (--p < 0) p = ringLen - 1; // backward through the recorded window
            }
        }
        tailReadIndex = (tailReadIndex - n + ringLen) % ringLen;
        swapFadeRanThisBlock = true;
        swapFadeSamplesThisBlock += n;
        fadeOutSamplesLeft -= n;
        if (fadeOutSamplesLeft <= 0)
        {
            swapFadeActive = false;
            tailRingSamples = 0; // recording resumes below -- only fresh new-graph output may arm the next fade
        }
    }
}

void AudioEngine::detectCallbackRealtime() noexcept
{
    bool realtime = false;
    if (const auto* t = juce::Thread::getCurrentThread())
        realtime = t->isRealtime();
   #if JUCE_LINUX
    else
    {
        // A backend that invokes the callback from a NATIVE thread
        // reports no juce::Thread at all -- ask the kernel directly.
        const int policy = sched_getscheduler (0);
        realtime = (policy == SCHED_FIFO || policy == SCHED_RR);
    }
   #endif

    callbackIsRealtime.store (realtime, std::memory_order_relaxed);
    callbackRealtimeDetected.store (true, std::memory_order_relaxed);

    // Stamp every graph this callback can reach BEFORE any of them
    // process this block: the published graph and every spill (active +
    // the pending hand-off). The swap fade's retired graph never runs --
    // its recorded tail is replayed instead -- so it needs no stamp.
    if (auto* g = graphSlot.currentRaw())
        g->setAudioCallerIsRealtime (realtime);
    spillover.setAudioCallerIsRealtime (realtime);
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    // A (re)opened device may invoke the callback from a DIFFERENT
    // thread or scheduling policy -- the old classification is stale,
    // re-detect on the new stream's first callback.
    callbackRealtimeDetected.store (false, std::memory_order_relaxed);
    callbackIsRealtime.store (false, std::memory_order_relaxed);

    activeGraph = nullptr; // the callback is not running: forget any graph that may have been swept while stopped
    swapFadeActive = false;
    fadeOutSamplesLeft = 0;
    tailRingWrite = tailRingSamples = tailReadIndex = 0;
    spillover.reset(); // spills were prepared for the old stream's rate/block size -- cut them all
    // Pre-sized here (control thread) so the swap fade in processCurrentGraph() never allocates on the audio
    // thread: the ring must hold the larger of one callback or the ~10 ms fade window.
    const double devRate = device->getCurrentSampleRate() > 0.0 ? device->getCurrentSampleRate() : 48000.0;
    outputTailRing.setSize (juce::jmax (1, device->getOutputChannelNames().size()),
                            juce::jmax (1, juce::jmax (device->getCurrentBufferSizeSamples(), (int) (0.010 * devRate))),
                            false, true, false);

    sampleRate.store (device->getCurrentSampleRate(), std::memory_order_relaxed);
    blockSize.store (device->getCurrentBufferSizeSamples(), std::memory_order_relaxed);
    pitchDetector.prepare (device->getCurrentSampleRate());
    masterEq.prepare (device->getCurrentSampleRate(), 2);
    masterGainSmooth.reset (device->getCurrentSampleRate(), 0.02); // ~20 ms ramp, see setMasterGainDb()
    masterGainSmooth.setCurrentAndTargetValue (masterGainLinear.load (std::memory_order_relaxed));

    // The graph that is already published was prepared for whatever rate the device ran at before (a rate change, a re-opened device);
    // the callback is not running yet, so preparing it here is safe. A processor whose rate did not change ignores the call.
    if (auto* graph = graphSlot.currentRaw())
        graph->prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples(), 2);
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
    // Flush denormals to zero for everything the audio thread runs (reverb tails, filter and NAM states decaying
    // toward silence): a denormal float costs 50-150x a normal one on x86, and a chain whose input goes quiet is
    // exactly when they appear.
    const juce::ScopedNoDenormals noDenormals;

    if (! callbackRealtimeDetected.load (std::memory_order_relaxed))
        detectCallbackRealtime();

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
        // Only while the tuner is on screen -- see setTunerActive(). This is now just a ring-buffer write; the analysis
        // itself runs on the control thread (timerCallback -> PitchDetector::analyse()).
        if (tunerActive.load (std::memory_order_relaxed))
            pitchDetector.pushSamples (in, numSamples);
        // Metered AFTER the input gain below (the IN meter is a gain-staging tool: it has to show what the chain is
        // actually being fed, which is what decides how hard every model is driven).
        const auto range = juce::FloatVectorOperations::findMinAndMax (in, numSamples);
        lastInputLevel.store (juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd()))
                                  * inputGainLinear.load (std::memory_order_relaxed),
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

    // Input sensitivity (see setInputGainDb()): applied to what the chain sees, not to the dry passthrough path -- the
    // buffer IS the chain's input at this point.
    const float inputGain = inputGainLinear.load (std::memory_order_relaxed);
    if (! juce::approximatelyEqual (inputGain, 1.0f))
        buffer.applyGain (inputGain);

    processGraph (buffer, numSamples);

    // Master section: the free-form EQ, then the output gain -- both post-graph,
    // so the OUT meter below reads the level actually about to reach the device.
    masterEq.process (buffer);

    masterGainSmooth.setTargetValue (masterGainLinear.load (std::memory_order_relaxed));
    if (masterGainSmooth.isSmoothing())
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = masterGainSmooth.getNextValue();
            for (int ch = 0; ch < numOutputChannels; ++ch)
                outputChannelData[ch][i] *= g;
        }
    }
    else
    {
        const float g = masterGainLinear.load (std::memory_order_relaxed);
        if (! juce::approximatelyEqual (g, 1.0f))
            buffer.applyGain (g);
    }

    // Final device boundary: the DAC hard-clips past full scale, so whatever the chain or master
    // section emits saturates into deviceSoftCeiling instead. Its knee sits at the model ceiling,
    // so an amp's already-limited emit passes through untouched -- only genuinely over-scale signal
    // (boosted last-in-chain pedal, summed lanes, hot master gain) gets caught. Post master EQ +
    // gain, so the OUT meter below still reads the level actually about to reach the device.
    for (int ch = 0; ch < numOutputChannels; ++ch)
    {
        auto* out = outputChannelData[ch];
        for (int i = 0; i < numSamples; ++i)
            out[i] = (float) deviceSoftCeiling (out[i]);
    }

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

    // Two numbers, because they answer different questions and the raw per-block ratio answers neither on its own (it
    // swings too much to read): an average over ~0.5 s for "how loaded am I", and a peak hold over ~1 s for "did any
    // single block come close to dropping out" -- one block over 100% IS a dropout.
    const double instant = blockSeconds > 0.0 ? elapsedSeconds / blockSeconds : 0.0;
    const double averageCoeff = 1.0 - std::exp (-blockSeconds / 0.5);
    const double previousAverage = averageCpuUsage.load (std::memory_order_relaxed);
    averageCpuUsage.store (previousAverage + averageCoeff * (instant - previousAverage), std::memory_order_relaxed);

    const double peakDecay = std::exp (-blockSeconds / 1.0);
    const double previousPeak = peakCpuUsage.load (std::memory_order_relaxed);
    peakCpuUsage.store (juce::jmax (instant, previousPeak * peakDecay), std::memory_order_relaxed);
}

void AudioEngine::timerCallback()
{
    graphSlot.sweep();
    spillover.sweep();
    masterEq.sweep();

    // YIN's O(window x tauMax) pass, on the control thread where unbounded work is allowed -- see PitchDetector::analyse().
    if (tunerActive.load (std::memory_order_relaxed))
        pitchDetector.analyse();
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
