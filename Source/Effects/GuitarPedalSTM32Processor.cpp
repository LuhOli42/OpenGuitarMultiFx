// GuitarPedalSTM32 (c) 2026 Keith Bloemer, MIT; reverse delay line (c) 2021 Adam Fulford, MIT; delay line after DaisySP (Electrosmith), MIT
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "GuitarPedalSTM32Processor.h"
#include "IconKit.h"

#include <IconData.h>

#include <algorithm>
#include <cmath>
#include <functional>

namespace openguitarmultifx
{

namespace
{
    struct FdnLineSpec
    {
        double baseSeconds;
        double depthSeconds;
        double rateHz;
        uint16_t seed;
    };

    constexpr std::array<FdnLineSpec, 8> fdnLineSpecs {{
        { 2473.0 / 48000.0, 0.0010, 3.100, 1966 },
        { 2767.0 / 48000.0, 0.0011, 3.500, 29491 },
        { 3217.0 / 48000.0, 0.0017, 1.110, 22937 },
        { 3557.0 / 48000.0, 0.0006, 3.973, 9830 },
        { 3907.0 / 48000.0, 0.0010, 2.341, 20643 },
        { 4127.0 / 48000.0, 0.0011, 1.897, 22937 },
        { 2143.0 / 48000.0, 0.0017, 0.891, 29491 },
        { 1933.0 / 48000.0, 0.0006, 3.221, 14417 }
    }};

    juce::AudioParameterFloatAttributes steppedLabels (std::function<juce::String (int)> name)
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [name] (float value, int) { return name (juce::roundToInt (value)); });
    }

    size_t wrapIndex (long long index, size_t length) noexcept
    {
        const auto signedLength = static_cast<long long> (length);
        auto wrapped = index % signedLength;
        if (wrapped < 0)
            wrapped += signedLength;
        return static_cast<size_t> (wrapped);
    }
}

GuitarPedalSTM32Processor::GuitarPedalSTM32Processor()
{
    auto revMix = std::make_unique<juce::AudioParameterFloat> ("gpstm_reverb_mix", "Reverb Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.4f);
    auto revTime = std::make_unique<juce::AudioParameterFloat> ("gpstm_reverb_time", "Reverb Time", juce::NormalisableRange<float> (0.0f, 1.0f), 0.7f);
    auto revFilter = std::make_unique<juce::AudioParameterFloat> ("gpstm_reverb_filter", "Reverb Filter", juce::NormalisableRange<float> (0.0f, 1.0f), 0.6f);
    auto delMix = std::make_unique<juce::AudioParameterFloat> ("gpstm_delay_mix", "Delay Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.35f);
    auto delTime = std::make_unique<juce::AudioParameterFloat> (
        "gpstm_delay_time", "Delay Time",
        juce::NormalisableRange<float> (1.0f, 1000.0f, 0.0f, 0.4f), 400.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms"));
    auto delFeedback = std::make_unique<juce::AudioParameterFloat> ("gpstm_delay_feedback", "Delay Feedback", juce::NormalisableRange<float> (0.0f, 1.0f), 0.35f);
    auto route = std::make_unique<juce::AudioParameterFloat> (
        "gpstm_routing", "Routing", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 0.0f,
        steppedLabels ([] (int value)
        {
            static const char* labels[] { "Dly>Rev", "Parallel", "Rev>Dly" };
            return juce::String (labels[juce::jlimit (0, 2, value)]);
        }));
    auto mode = std::make_unique<juce::AudioParameterFloat> (
        "gpstm_delay_mode", "Delay Mode", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 0.0f,
        steppedLabels ([] (int value)
        {
            static const char* labels[] { "Normal", "Octave", "Reverse" };
            return juce::String (labels[juce::jlimit (0, 2, value)]);
        }));
    auto held = std::make_unique<juce::AudioParameterFloat> (
        "gpstm_hold", "Hold", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        steppedLabels ([] (int value) { return value == 0 ? juce::String ("Off") : juce::String ("On"); }));

    reverbMix = revMix.get();
    reverbTime = revTime.get();
    reverbFilter = revFilter.get();
    delayMix = delMix.get();
    delayTime = delTime.get();
    delayFeedback = delFeedback.get();
    routing = route.get();
    delayMode = mode.get();
    hold = held.get();
    registerTempoSyncParam (delTime.get());

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "gpstm", "GuitarML STM32", "|", std::move (revMix), std::move (revTime), std::move (revFilter),
        std::move (delMix), std::move (delTime), std::move (delFeedback), std::move (route), std::move (mode), std::move (held));
}

void GuitarPedalSTM32Processor::prepare (double sampleRate, int, int)
{
    currentSampleRate = sampleRate;
    if (sampleRate <= 0.0)
    {
        reset();
        return;
    }

    const auto rateScale = sampleRate / 48000.0;
    const auto forwardLength = static_cast<size_t> (std::max (1.0, std::round (sampleRate)));
    forwardDelay.assign (forwardLength, 0.0f);
    forwardWritePosition = 0;

    const auto reverseLength = static_cast<size_t> (std::max (2.0, std::round (sampleRate * 2.0)));
    reverseDelay = {};
    reverseDelay.samples.assign (reverseLength, 0.0f);
    reverseDelay.fadeSamples = static_cast<size_t> (std::max (1.0, std::round (2300.0 * rateScale)));
    reverseDelay.delaySamples = reverseDelay.fadeSamples + 100;

    for (size_t i = 0; i < fdnLines.size(); ++i)
    {
        auto& line = fdnLines[i];
        const auto& spec = fdnLineSpecs[i];
        line.baseDelaySamples = spec.baseSeconds * sampleRate;
        line.depthSamples = spec.depthSeconds * sampleRate;
        line.samples.assign (static_cast<size_t> (std::ceil (line.baseDelaySamples + line.depthSamples)) + 4, 0.0f);
        line.writePosition = 0;
        line.randomState = spec.seed;
        line.segmentLength = static_cast<size_t> (std::max (1.0, std::round (sampleRate / spec.rateHz)));
        line.segmentPosition = 0;
        line.currentVariation = 0.0;
        line.segmentStartVariation = 0.0;
        line.targetVariation = static_cast<double> (nextFdnRandom (line)) * line.depthSamples;
    }

    smoothedReverbMix.reset (sampleRate, paramRampSeconds);
    smoothedReverbMix.setCurrentAndTargetValue (reverbMix->get());
    smoothedDelayMix.reset (sampleRate, paramRampSeconds);
    smoothedDelayMix.setCurrentAndTargetValue (delayMix->get());

    currentDelaySamples = juce::jlimit (1.0, static_cast<double> (forwardLength - 2),
                                        static_cast<double> (delayTime->get() * 0.001f) * sampleRate);
    delaySmoothingCoefficient = 1.0 - std::pow (1.0 - 0.0002, 48000.0 / sampleRate);

    const auto g = std::tan (juce::MathConstants<double>::pi * 1000.0 / sampleRate);
    const auto k = 1.0 / 0.7071067811865476;
    const auto a1 = 1.0 / (1.0 + g * (g + k));
    const auto a2 = g * a1;
    const auto a3 = g * a2;
    octaveA1 = static_cast<float> (a1);
    octaveA2 = static_cast<float> (a2);
    octaveA3 = static_cast<float> (a3);

    reset();
}

void GuitarPedalSTM32Processor::reset()
{
    std::fill (forwardDelay.begin(), forwardDelay.end(), 0.0f);
    forwardWritePosition = 0;
    reverseDelay.writePosition = 0;
    reverseDelay.readPosition1 = 0;
    reverseDelay.readPosition2 = 0;
    reverseDelay.headDifference = 0;
    reverseDelay.fading = false;
    reverseDelay.playingHead2 = false;
    reverseDelay.fadePosition = 0.0f;
    std::fill (reverseDelay.samples.begin(), reverseDelay.samples.end(), 0.0f);

    fdnFilterStates.fill (0.0f);
    for (size_t i = 0; i < fdnLines.size(); ++i)
    {
        auto& line = fdnLines[i];
        std::fill (line.samples.begin(), line.samples.end(), 0.0f);
        line.writePosition = 0;
        line.randomState = fdnLineSpecs[i].seed;
        line.segmentPosition = 0;
        line.currentVariation = 0.0;
        line.segmentStartVariation = 0.0;
        line.targetVariation = static_cast<double> (nextFdnRandom (line)) * line.depthSamples;
    }

    octaveIc1 = 0.0f;
    octaveIc2 = 0.0f;
}

void GuitarPedalSTM32Processor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0 || forwardDelay.empty() || reverseDelay.samples.empty())
        return;

    const auto numChannels = buffer.getNumChannels();
    const auto numSamples = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return;

    smoothedReverbMix.setTargetValue (reverbMix->get());
    smoothedDelayMix.setTargetValue (delayMix->get());

    const auto held = hold->get() >= 0.5f;
    const auto reverbFeedback = held ? 1.0f : juce::jlimit (0.0f, 1.0f, reverbTime->get());
    const auto delayFeedbackValue = held ? 1.0f : juce::jlimit (0.0f, 1.0f, delayFeedback->get());
    const auto mode = juce::jlimit (0, 2, juce::roundToInt (delayMode->get()));
    const auto route = juce::jlimit (0, 2, juce::roundToInt (routing->get()));

    auto cutoff = static_cast<double> (reverbFilter->get()) * static_cast<double> (reverbFilter->get()) * 16000.0 + 500.0;
    cutoff = std::min (cutoff, 0.45 * currentSampleRate);
    const auto c = 2.0 - std::cos (2.0 * juce::MathConstants<double>::pi * cutoff / currentSampleRate);
    const auto damping = static_cast<float> (c - std::sqrt (std::max (0.0, c * c - 1.0)));
    const auto targetDelay = juce::jlimit (1.0, static_cast<double> (forwardDelay.size() - 2),
                                           static_cast<double> (delayTime->get() * 0.001f) * currentSampleRate);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float input = 0.0f;
        for (int channel = 0; channel < numChannels; ++channel)
            input += buffer.getSample (channel, sample);
        input /= static_cast<float> (numChannels);

        currentDelaySamples += delaySmoothingCoefficient * (targetDelay - currentDelaySamples);
        const auto effectInput = held ? 0.0f : input;
        const auto reverbWet = smoothedReverbMix.getNextValue();
        const auto delayWet = smoothedDelayMix.getNextValue();
        float output = input;

        if (route == 1)
        {
            const auto reverbOutput = processReverb (effectInput, reverbFeedback, damping);
            const auto delayOutput = processDelay (effectInput, delayFeedbackValue, mode);
            output += reverbWet * reverbOutput + delayWet * delayOutput;
        }
        else if (route == 0)
        {
            const auto delayOutput = processDelay (effectInput, delayFeedbackValue, mode) * delayWet;
            const auto reverbOutput = processReverb (delayOutput + effectInput, reverbFeedback, damping);
            output += reverbWet * reverbOutput;
        }
        else
        {
            const auto reverbOutput = processReverb (effectInput, reverbFeedback, damping);
            const auto delayOutput = processDelay (reverbOutput * reverbWet + effectInput, delayFeedbackValue, mode);
            output += delayWet * delayOutput;
        }

        for (int channel = 0; channel < numChannels; ++channel)
            buffer.setSample (channel, sample, output);
    }
}

float GuitarPedalSTM32Processor::processReverb (float input, float feedback, float damping) noexcept
{
    float sum = 0.0f;
    for (const auto state : fdnFilterStates)
        sum += state;
    const auto junction = 0.25f * sum;
    const auto inL = input + junction;
    const auto inR = input + junction;
    float outL = 0.0f;
    float outR = 0.0f;

    for (size_t i = 0; i < fdnLines.size(); ++i)
    {
        auto& line = fdnLines[i];
        line.samples[line.writePosition] = juce::jlimit (-1.0f, 1.0f,
                                                        (i % 2 == 0 ? inL : inR) - fdnFilterStates[i]);
        const auto delayed = interpolateFdnLine (line, line.baseDelaySamples + line.currentVariation);
        const auto v = delayed * feedback;
        fdnFilterStates[i] = juce::jlimit (-1.0f, 1.0f, v + damping * (fdnFilterStates[i] - v));
        if ((i % 2) == 0)
            outL += fdnFilterStates[i];
        else
            outR += fdnFilterStates[i];
        if (++line.writePosition >= line.samples.size())
            line.writePosition = 0;
        advanceFdnWander (line);
    }

    outL *= 0.35f;
    outR *= 0.35f;
    return 0.5f * (outL + outR);
}

float GuitarPedalSTM32Processor::processDelay (float input, float feedback, int mode) noexcept
{
    const auto forwardRead = readForwardDelay (mode == 1 ? 2 : 1);
    const auto reverseRead = readReverseDelay();
    auto selectedRead = forwardRead;
    if (mode == 1)
    {
        const auto v3 = forwardRead - octaveIc2;
        const auto v1 = octaveA1 * octaveIc1 + octaveA2 * v3;
        const auto v2 = octaveIc2 + octaveA2 * octaveIc1 + octaveA3 * v3;
        octaveIc1 = 2.0f * v1 - octaveIc1;
        octaveIc2 = 2.0f * v2 - octaveIc2;
        selectedRead = v2;
    }
    else if (mode == 2)
    {
        selectedRead = reverseRead;
    }

    const auto writeValue = juce::jlimit (-1.0f, 1.0f, feedback * selectedRead + input);
    forwardDelay[forwardWritePosition] = writeValue;
    if (forwardWritePosition == 0)
        forwardWritePosition = forwardDelay.size() - 1;
    else
        --forwardWritePosition;

    const auto maxReverseDelay = reverseDelay.samples.size() - 1;
    const auto minimumReverseDelay = reverseDelay.fadeSamples + 100;
    reverseDelay.delaySamples = juce::jlimit (minimumReverseDelay, maxReverseDelay,
                                               static_cast<size_t> (std::llround (2.0 * currentDelaySamples)));
    writeReverseDelay (writeValue);
    return selectedRead;
}

float GuitarPedalSTM32Processor::readForwardDelay (int speed) const noexcept
{
    const auto delayWhole = static_cast<size_t> (std::floor (currentDelaySamples));
    const auto fraction = static_cast<float> (currentDelaySamples - static_cast<double> (delayWhole));
    const auto length = forwardDelay.size();
    const auto readPosition = (static_cast<size_t> (static_cast<uint64_t> (forwardWritePosition) * static_cast<uint64_t> (speed))
                               + delayWhole) % length;
    const auto nextPosition = readPosition + 1 == length ? 0 : readPosition + 1;
    return forwardDelay[readPosition] + fraction * (forwardDelay[nextPosition] - forwardDelay[readPosition]);
}

float GuitarPedalSTM32Processor::readReverseDelay() const noexcept
{
    const auto fade = std::sin (reverseDelay.fadePosition * juce::MathConstants<float>::halfPi);
    const auto otherFade = std::sin ((1.0f - reverseDelay.fadePosition) * juce::MathConstants<float>::halfPi);
    return reverseDelay.samples[reverseDelay.readPosition2] * fade
         + reverseDelay.samples[reverseDelay.readPosition1] * otherFade;
}

void GuitarPedalSTM32Processor::writeReverseDelay (float value) noexcept
{
    auto& state = reverseDelay;
    const auto length = state.samples.size();
    state.samples[state.writePosition] = value;
    if (++state.writePosition >= length)
        state.writePosition = 0;

    if (state.delaySamples > 0)
        state.headDifference = (state.headDifference + 1) % state.delaySamples;

    state.readPosition1 = state.readPosition1 == 0 ? length - 1 : state.readPosition1 - 1;
    state.readPosition2 = state.readPosition2 == 0 ? length - 1 : state.readPosition2 - 1;

    const auto fadeStart = state.delaySamples - state.fadeSamples - 1;
    if (state.headDifference > fadeStart)
    {
        if (! state.fading)
        {
            state.fading = true;
            if (! state.playingHead2)
                state.readPosition2 = state.writePosition == 0 ? length - 1 : state.writePosition - 1;
            else
                state.readPosition1 = state.writePosition == 0 ? length - 1 : state.writePosition - 1;
        }

    }

    if (state.fading && ! state.playingHead2)
    {
        state.fadePosition += 1.0f / static_cast<float> (state.fadeSamples);
        if (state.fadePosition > 1.0f)
        {
            state.fadePosition = 1.0f;
            state.fading = false;
            state.playingHead2 = true;
        }
    }
    else if (state.fading)
    {
        state.fadePosition -= 1.0f / static_cast<float> (state.fadeSamples);
        if (state.fadePosition < 0.0f)
        {
            state.fadePosition = 0.0f;
            state.fading = false;
            state.playingHead2 = false;
        }
    }
}

void GuitarPedalSTM32Processor::advanceFdnWander (FdnLine& line) noexcept
{
    if (++line.segmentPosition >= line.segmentLength)
    {
        line.currentVariation = line.targetVariation;
        line.segmentStartVariation = line.currentVariation;
        line.targetVariation = static_cast<double> (nextFdnRandom (line)) * line.depthSamples;
        line.segmentPosition = 0;
    }
    else
    {
        const auto amount = static_cast<double> (line.segmentPosition) / static_cast<double> (line.segmentLength);
        line.currentVariation = line.segmentStartVariation + amount * (line.targetVariation - line.segmentStartVariation);
    }
}

float GuitarPedalSTM32Processor::nextFdnRandom (FdnLine& line) noexcept
{
    const auto next = (static_cast<uint32_t> (line.randomState) * 15625u + 1u) & 0xffffu;
    line.randomState = static_cast<uint16_t> (next);
    return static_cast<float> (static_cast<double> (line.randomState) / 32768.0 - 1.0);
}

float GuitarPedalSTM32Processor::interpolateFdnLine (const FdnLine& line, double delaySamples) noexcept
{
    const auto integerDelay = static_cast<long long> (std::floor (delaySamples));
    const auto fraction = delaySamples - static_cast<double> (integerDelay);
    auto base = static_cast<long long> (line.writePosition) - integerDelay;
    auto t = fraction;
    if (fraction > 0.0)
    {
        --base;
        t = 1.0 - fraction;
    }

    const auto length = line.samples.size();
    const auto center = wrapIndex (base, length);
    const auto previous = center == 0 ? length - 1 : center - 1;
    const auto next = center + 1 == length ? 0 : center + 1;
    const auto next2 = next + 1 == length ? 0 : next + 1;
    const auto x = t;
    const auto c0 = -x * (x - 1.0) * (x - 2.0) / 6.0;
    const auto c1 = (x + 1.0) * (x - 1.0) * (x - 2.0) / 2.0;
    const auto c2 = -(x + 1.0) * x * (x - 2.0) / 2.0;
    const auto c3 = (x + 1.0) * x * (x - 1.0) / 6.0;
    return static_cast<float> (c0 * line.samples[previous] + c1 * line.samples[center]
                               + c2 * line.samples[next] + c3 * line.samples[next2]);
}

void GuitarPedalSTM32Processor::drawIcon (juce::Graphics& g, juce::Rectangle<float> bounds) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::gpstm_pedal_svg, IconData::gpstm_pedal_svgSize);
    icon::drawSvg (g, bounds, svg.get());
}

} // namespace openguitarmultifx
