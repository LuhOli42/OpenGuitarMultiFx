#include "HoldProcessor.h"
#include "IconKit.h"

#include <IconData.h>

#include <array>

namespace openguitarmultifx
{

HoldProcessor::HoldProcessor()
{
    auto holdParam = std::make_unique<juce::AudioParameterFloat> (
        "hold_engage", "Hold", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f);
    auto captureParam = std::make_unique<juce::AudioParameterFloat> (
        "hold_capture", "Capture", juce::NormalisableRange<float> (20.0f, maxCaptureMs), 300.0f);
    auto mixParam = std::make_unique<juce::AudioParameterFloat> (
        "hold_mix", "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f);

    hold = holdParam.get();
    captureMs = captureParam.get();
    mix = mixParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "hold", "Hold", "|",
        std::move (holdParam), std::move (captureParam), std::move (mixParam));
}

void HoldProcessor::prepare (double sampleRate, int, int numChannels)
{
    currentSampleRate = sampleRate;

    const int bufferLength = (int) std::ceil (maxCaptureMs * 0.001 * sampleRate) + 4;
    circularBuffer.setSize (juce::jmax (1, numChannels), bufferLength, false, true, true);

    reset();
}

void HoldProcessor::reset()
{
    circularBuffer.clear();
    writePos = 0;
    wasHeld = false;
    loopStartPos = 0;
    loopOffset = 0;
    frozenLoopLengthSamples = 0;
}

void HoldProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0 || circularBuffer.getNumSamples() == 0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), circularBuffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();
    const int bufferLength = circularBuffer.getNumSamples();
    const bool isHeld = hold->get() >= 0.5f;
    const float wetAmount = mix->get();

    if (isHeld && ! wasHeld)
    {
        // Rising edge: freeze the last `captureMs` of whatever's already in
        // the buffer, starting the loop from its oldest sample so playback
        // continues in the same order it was recorded, not backwards.
        frozenLoopLengthSamples = juce::jlimit (1, bufferLength,
            (int) (captureMs->get() * 0.001f * (float) currentSampleRate));
        loopStartPos = ((writePos - frozenLoopLengthSamples) % bufferLength + bufferLength) % bufferLength;
        loopOffset = 0;
    }
    wasHeld = isHeld;

    std::array<float*, 2> ioData {};
    std::array<float*, 2> loopData {};
    for (int ch = 0; ch < numChannels; ++ch)
    {
        ioData[(size_t) ch] = buffer.getWritePointer (ch);
        loopData[(size_t) ch] = circularBuffer.getWritePointer (ch);
    }

    for (int i = 0; i < numSamples; ++i)
    {
        if (isHeld)
        {
            // loopStartPos and loopOffset are each individually < bufferLength,
            // so their sum needs at most one wraparound subtraction.
            int readIndex = loopStartPos + loopOffset;
            if (readIndex >= bufferLength)
                readIndex -= bufferLength;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float wet = loopData[(size_t) ch][readIndex];
                ioData[(size_t) ch][i] = ioData[(size_t) ch][i] * (1.0f - wetAmount) + wet * wetAmount;
            }
            if (++loopOffset >= frozenLoopLengthSamples)
                loopOffset = 0;
        }
        else
        {
            // Not held -- Hold is a bypass until engaged (output is
            // untouched), just continuously recording what a rising edge
            // would capture (see class doc comment).
            for (int ch = 0; ch < numChannels; ++ch)
                loopData[(size_t) ch][writePos] = ioData[(size_t) ch][i];
            if (++writePos >= bufferLength)
                writePos = 0;
        }
    }
}

void HoldProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/hold.svg -- the
    // unified icon set's "Hold" glyph (Delay category).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::hold_svg, IconData::hold_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
