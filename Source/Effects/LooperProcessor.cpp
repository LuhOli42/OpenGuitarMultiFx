#include "LooperProcessor.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

LooperProcessor::LooperProcessor()
{
    auto triggerParam = std::make_unique<juce::AudioParameterFloat> (
        "looper_trigger", "Trigger", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f);
    auto clearParam = std::make_unique<juce::AudioParameterFloat> (
        "looper_clear", "Clear", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f);
    auto decayParam = std::make_unique<juce::AudioParameterFloat> (
        "looper_overdub_decay", "Overdub Decay", juce::NormalisableRange<float> (0.5f, 1.0f), 0.98f);
    auto levelParam = std::make_unique<juce::AudioParameterFloat> (
        "looper_level", "Level", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f);

    trigger = triggerParam.get();
    clear = clearParam.get();
    overdubDecay = decayParam.get();
    level = levelParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "looper", "Looper", "|",
        std::move (triggerParam), std::move (clearParam), std::move (decayParam), std::move (levelParam));
}

void LooperProcessor::prepare (double sampleRate, int, int numChannels)
{
    currentSampleRate = sampleRate;

    const int bufferLength = (int) (maxLoopSeconds * sampleRate);
    loopBuffer.setSize (juce::jmax (1, numChannels), bufferLength, false, true, true);

    smoothedLevel.reset (sampleRate, paramRampSeconds);
    smoothedLevel.setCurrentAndTargetValue (level->get());
    smoothedOverdubDecay.reset (sampleRate, paramRampSeconds);
    smoothedOverdubDecay.setCurrentAndTargetValue (overdubDecay->get());

    reset();
}

void LooperProcessor::reset()
{
    loopBuffer.clear();
    state = State::idle;
    triggerWasHigh = false;
    clearWasHigh = false;
    writePos = 0;
    loopLengthSamples = 0;
}

void LooperProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0 || loopBuffer.getNumSamples() == 0)
        return;

    // Edge-detected once per process() call, not per sample -- a few ms
    // of imprecision on exactly when a press registers is imperceptible
    // for a footswitch-driven control, and per-sample edge tracking would
    // add nothing but complexity here.
    const bool triggerHigh = trigger->get() >= 0.5f;
    const bool triggerEdge = triggerHigh && ! triggerWasHigh;
    triggerWasHigh = triggerHigh;

    const bool clearHigh = clear->get() >= 0.5f;
    const bool clearEdge = clearHigh && ! clearWasHigh;
    clearWasHigh = clearHigh;

    if (clearEdge)
    {
        state = State::idle;
        writePos = 0;
        loopLengthSamples = 0;
        loopBuffer.clear();
    }

    if (triggerEdge)
    {
        switch (state)
        {
            case State::idle:
                state = State::recording;
                writePos = 0;
                loopBuffer.clear();
                break;

            case State::recording:
                // A near-zero-length loop (an accidental double-press)
                // isn't useful -- treat it as "never really started" and
                // drop back to idle instead of looping near-silence.
                if (writePos > (int) (0.05 * currentSampleRate))
                {
                    loopLengthSamples = writePos;
                    writePos = 0;
                    state = State::playing;
                }
                else
                {
                    state = State::idle;
                    writePos = 0;
                }
                break;

            case State::playing:
                state = State::overdubbing;
                break;

            case State::overdubbing:
                state = State::playing;
                break;
        }
    }

    const int numChannels = juce::jmin (buffer.getNumChannels(), loopBuffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();
    const int bufferCapacity = loopBuffer.getNumSamples();
    // trigger/clear are edge-detected footswitches and must act on their
    // edge, so they read the parameter directly (and stay unsmoothed).
    // Level multiplies the loop into the live signal -> per-sample ramp;
    // overdub decay only affects what is written back, so per-block is
    // enough.
    smoothedLevel.setTargetValue (level->get());
    smoothedOverdubDecay.setTargetValue (overdubDecay->get());
    const float decay = smoothedOverdubDecay.skip (numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const float lvl = smoothedLevel.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            auto* loopData = loopBuffer.getWritePointer (ch);
            const float input = data[i];
            float loopSample = 0.0f;

            switch (state)
            {
                case State::idle:
                    break;

                case State::recording:
                    if (writePos < bufferCapacity)
                        loopData[writePos] = input;
                    break;

                case State::playing:
                    if (loopLengthSamples > 0)
                        loopSample = loopData[writePos];
                    break;

                case State::overdubbing:
                    if (loopLengthSamples > 0)
                    {
                        loopSample = loopData[writePos];
                        loopData[writePos] = loopSample * decay + input;
                    }
                    break;
            }

            // The loop always ADDS to the dry signal -- a looper never
            // mutes your live playing, it layers underneath it.
            data[i] = input + loopSample * lvl;
        }

        if (state == State::recording)
        {
            ++writePos;
            if (writePos >= bufferCapacity)
            {
                // Safety net: the max loop length was reached without a
                // second trigger press -- stop recording rather than
                // silently dropping audio past the buffer's end.
                loopLengthSamples = bufferCapacity;
                writePos = 0;
                state = State::playing;
            }
        }
        else if ((state == State::playing || state == State::overdubbing) && loopLengthSamples > 0)
        {
            ++writePos;
            if (writePos >= loopLengthSamples)
                writePos -= loopLengthSamples;
        }
    }
}

juce::String LooperProcessor::getStatusText() const
{
    switch (state)
    {
        case State::idle: return "Idle";
        case State::recording: return "Recording";
        case State::playing: return "Playing";
        case State::overdubbing: return "Overdubbing";
    }
    return {};
}

void LooperProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/looper.svg.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::looper_svg, IconData::looper_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
