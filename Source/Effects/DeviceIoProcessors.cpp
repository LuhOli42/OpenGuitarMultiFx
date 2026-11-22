#include "DeviceIoProcessors.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double rampSeconds = 0.010;
    constexpr float silenceDb = -60.0f;   // the bottom of a Level knob is silence

    float gainFromDb (float db) noexcept { return db <= silenceDb ? 0.0f : juce::Decibels::decibelsToGain (db); }

    juce::String dbText (float v)
    {
        return v <= silenceDb ? juce::String ("-inf") : juce::String (v, 1) + " dB";
    }
}

juce::String DeviceIoChoices::outputName (int choice)
{
    const int first = outputPairStart (choice) + 1;
    switch (outputChannels (choice))
    {
        case 1:  return "Out " + juce::String (first) + " (L)";
        case 2:  return "Out " + juce::String (first + 1) + " (R)";
        default: return "Out " + juce::String (first) + "/" + juce::String (first + 1);
    }
}

juce::String DeviceIoChoices::inputName (int choice)
{
    const int first = inputFirstChannel (choice) + 1;
    return inputIsPair (choice) ? "In " + juce::String (first) + "/" + juce::String (first + 1)
                                : "In " + juce::String (first);
}

// ---------------------------------------------------------------------------------------------------------------------

OutputSendProcessor::OutputSendProcessor()
{
    auto destParam = std::make_unique<juce::AudioParameterFloat> (
        "outsend_dest", "Output", juce::NormalisableRange<float> (0.0f, (float) DeviceIoChoices::numOutputChoices - 1.0f, 1.0f), 3.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return DeviceIoChoices::outputName (juce::jlimit (0, DeviceIoChoices::numOutputChoices - 1, juce::roundToInt (v)));
        }));
    auto levelParam = std::make_unique<juce::AudioParameterFloat> (
        "outsend_level", "Level", juce::NormalisableRange<float> (silenceDb, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return dbText (v); }));
    auto thruParam = std::make_unique<juce::AudioParameterFloat> (
        "outsend_mute_thru", "Mute Thru", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return v > 0.5f ? juce::String ("On") : juce::String ("Off"); }));

    destination = destParam.get();
    levelDb = levelParam.get();
    muteThru = thruParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("outsend", "Output Send", "|", std::move (destParam),
                                                                         std::move (levelParam), std::move (thruParam));
}

void DeviceOutputSend::prepare (double sampleRate)
{
    gain.reset (sampleRate, rampSeconds);
    started = false;
}

void DeviceOutputSend::process (EffectProcessor::DeviceIo* io, const float* inL, const float* inR, int numSamples,
                                int wanted, float targetGain) noexcept
{
    wanted = juce::jlimit (0, DeviceIoChoices::numOutputChoices - 1, wanted);
    if (! started)
    {
        started = true;
        activeDestination = wanted;
        gain.setCurrentAndTargetValue (targetGain);
    }

    // A new destination: fade the old one out first, switch once silent (end of block), fade the new one in.
    gain.setTargetValue (wanted == activeDestination ? targetGain : 0.0f);

    auto* outputs = io != nullptr ? io->outputs : nullptr;
    const int pairStart = DeviceIoChoices::outputPairStart (activeDestination);
    const int mode = DeviceIoChoices::outputChannels (activeDestination);
    const bool canSend = outputs != nullptr && pairStart + 1 < outputs->getNumChannels() && numSamples <= outputs->getNumSamples();

    float* left = canSend ? outputs->getWritePointer (pairStart) : nullptr;
    float* right = canSend ? outputs->getWritePointer (pairStart + 1) : nullptr;

    bool sent = false;
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = gain.getNextValue();
        if (canSend && g > 0.0f)
        {
            sent = true;
            switch (mode)
            {
                case 1:  left[i] += 0.5f * g * (inL[i] + inR[i]); break;   // one channel: the signal folded to mono
                case 2:  right[i] += 0.5f * g * (inL[i] + inR[i]); break;
                default: left[i] += g * inL[i]; right[i] += g * inR[i]; break;
            }
        }
    }

    if (sent)
        io->outputMask |= mode == 1 ? (1u << pairStart) : mode == 2 ? (1u << (pairStart + 1)) : (3u << pairStart);

    if (wanted != activeDestination && gain.getCurrentValue() <= 0.0f)
        activeDestination = wanted;
}

// ---------------------------------------------------------------------------------------------------------------------

void OutputSendProcessor::prepare (double sampleRate, int, int)
{
    send.prepare (sampleRate);
    thruGain.reset (sampleRate, rampSeconds);
    reset();
}

void OutputSendProcessor::reset()
{
    send.reset();
    started = false;
}

void OutputSendProcessor::process (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numChannels <= 0)
        return;

    const bool on = ! isBypassed(); // still called during the bypass crossfade: fade the send out with it
    const float thru = muteThru->get() > 0.5f ? 0.0f : 1.0f;
    if (! started)
    {
        started = true;
        thruGain.setCurrentAndTargetValue (thru);
    }
    thruGain.setTargetValue (thru);

    send.process (deviceIo, buffer.getReadPointer (0), buffer.getReadPointer (juce::jmin (1, numChannels - 1)), numSamples,
                  juce::roundToInt (destination->get()), on ? gainFromDb (levelDb->get()) : 0.0f);

    if (thruGain.isSmoothing())
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float t = thruGain.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.getWritePointer (ch)[i] *= t;
        }
    }
    else if (thruGain.getCurrentValue() <= 0.0f)
        buffer.clear (0, numSamples);
}

void OutputSendProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::output_send_svg, IconData::output_send_svgSize);
    icon::drawSvg (g, b, svg.get());
}

// ---------------------------------------------------------------------------------------------------------------------

InputSourceProcessor::InputSourceProcessor()
{
    auto srcParam = std::make_unique<juce::AudioParameterFloat> (
        "insource_src", "Input", juce::NormalisableRange<float> (0.0f, (float) DeviceIoChoices::numInputChoices - 1.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return DeviceIoChoices::inputName (juce::jlimit (0, DeviceIoChoices::numInputChoices - 1, juce::roundToInt (v)));
        }));
    auto levelParam = std::make_unique<juce::AudioParameterFloat> (
        "insource_level", "Level", juce::NormalisableRange<float> (silenceDb, 24.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return dbText (v); }));
    auto mixParam = std::make_unique<juce::AudioParameterFloat> (
        "insource_mix", "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            const int pct = juce::roundToInt (v * 100.0f);
            if (pct == 0)   return juce::String ("Chain only");
            if (pct == 50)  return juce::String ("Both");
            if (pct == 100) return juce::String ("Input only");
            return juce::String (pct) + "%";
        }));

    source = srcParam.get();
    levelDb = levelParam.get();
    mix = mixParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("insource", "Input Source", "|", std::move (srcParam),
                                                                         std::move (levelParam), std::move (mixParam));
}

void InputSourceProcessor::prepare (double sampleRate, int, int)
{
    inputGain.reset (sampleRate, rampSeconds);
    chainGain.reset (sampleRate, rampSeconds);
    reset();
}

void InputSourceProcessor::reset()
{
    started = false;
}

void InputSourceProcessor::process (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numChannels <= 0)
        return;

    const int wanted = juce::jlimit (0, DeviceIoChoices::numInputChoices - 1, juce::roundToInt (source->get()));
    const bool on = ! isBypassed();
    const float m = mix->get();
    const float level = gainFromDb (levelDb->get()) * InputSourceProcessor::inputGainForMix (m);

    if (! started)
    {
        started = true;
        activeSource = wanted;
        inputGain.setCurrentAndTargetValue (on ? level : 0.0f);
        chainGain.setCurrentAndTargetValue (chainGainForMix (m));
    }

    inputGain.setTargetValue (on && wanted == activeSource ? level : 0.0f);
    chainGain.setTargetValue (chainGainForMix (m));

    const float* a = nullptr;
    const float* b = nullptr;
    float deviceGain = 1.0f;
    if (deviceIo != nullptr && deviceIo->inputs != nullptr)
    {
        const int first = DeviceIoChoices::inputFirstChannel (activeSource);
        const int second = DeviceIoChoices::inputIsPair (activeSource) ? first + 1 : first;
        if (first < deviceIo->numInputs)
        {
            a = deviceIo->inputs[first];
            b = second < deviceIo->numInputs ? deviceIo->inputs[second] : a;
        }
        deviceGain = deviceIo->inputGain;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const float c = chainGain.getNextValue();
        const float g = inputGain.getNextValue() * deviceGain;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float in = 0.0f;
            if (a != nullptr)
                in = numChannels == 1 ? 0.5f * (a[i] + b[i]) : (ch == 0 ? a[i] : b[i]);
            auto* data = buffer.getWritePointer (ch);
            data[i] = data[i] * c + in * g;
        }
    }

    if (wanted != activeSource && inputGain.getCurrentValue() <= 0.0f)
        activeSource = wanted;
}

void InputSourceProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::input_source_svg, IconData::input_source_svgSize);
    icon::drawSvg (g, b, svg.get());
}

// ---------------------------------------------------------------------------------------------------------------------

CrossoverProcessor::CrossoverProcessor()
{
    auto freqParam = std::make_unique<juce::AudioParameterFloat> (
        "xover_freq", "Freq", juce::NormalisableRange<float> (40.0f, 5000.0f, 1.0f, 0.3f), 200.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
        }));
    auto bandParam = std::make_unique<juce::AudioParameterFloat> (
        "xover_band", "Send", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), (float) lows,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return v > 0.5f ? juce::String ("Highs") : juce::String ("Lows"); }));
    auto destParam = std::make_unique<juce::AudioParameterFloat> (
        "xover_dest", "Output", juce::NormalisableRange<float> (0.0f, (float) DeviceIoChoices::numOutputChoices - 1.0f, 1.0f), 3.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return DeviceIoChoices::outputName (juce::jlimit (0, DeviceIoChoices::numOutputChoices - 1, juce::roundToInt (v)));
        }));
    auto levelParam = std::make_unique<juce::AudioParameterFloat> (
        "xover_level", "Level", juce::NormalisableRange<float> (silenceDb, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return dbText (v); }));
    auto chainParam = std::make_unique<juce::AudioParameterFloat> (
        "xover_chain", "Chain", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return v > 0.5f ? juce::String ("Full range") : juce::String ("Other band"); }));

    frequency = freqParam.get();
    sendBand = bandParam.get();
    destination = destParam.get();
    levelDb = levelParam.get();
    chainMode = chainParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("xover", "Crossover", "|", std::move (freqParam),
                                                                         std::move (bandParam), std::move (destParam),
                                                                         std::move (levelParam), std::move (chainParam));
}

void CrossoverProcessor::prepare (double newSampleRate, int maxBlockSize, int)
{
    sampleRate = newSampleRate;
    smoothedFrequency.reset (sampleRate, 0.030);
    chainSplit.reset (sampleRate, rampSeconds);
    send.prepare (sampleRate);
    sendScratch.setSize (2, juce::jmax (1, maxBlockSize), false, false, true);
    reset();
}

void CrossoverProcessor::reset()
{
    for (auto& f : filters)
        f.clear();
    send.reset();
    started = false;
}

void CrossoverProcessor::updateCoefficients (float hz) noexcept
{
    const float fc = juce::jlimit (10.0f, (float) (0.45 * sampleRate), hz);
    const float g = std::tan (juce::MathConstants<float>::pi * fc / (float) sampleRate);
    constexpr float k = juce::MathConstants<float>::sqrt2;   // 1/Q, Q = 1/sqrt(2): Butterworth
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

void CrossoverProcessor::process (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    if (numChannels <= 0 || numSamples > sendScratch.getNumSamples())
        return;

    const bool on = ! isBypassed();
    const int wantedBand = sendBand->get() > 0.5f ? highs : lows;
    const bool chainKeepsOther = chainMode->get() < 0.5f;

    if (! started)
    {
        started = true;
        activeBand = wantedBand;
        smoothedFrequency.setCurrentAndTargetValue (frequency->get());
        chainSplit.setCurrentAndTargetValue (on && chainKeepsOther ? 1.0f : 0.0f);
        updateCoefficients (smoothedFrequency.getCurrentValue());
    }

    const bool swapping = wantedBand != activeBand;
    smoothedFrequency.setTargetValue (frequency->get());
    chainSplit.setTargetValue (on && chainKeepsOther && ! swapping ? 1.0f : 0.0f);

    constexpr float k = juce::MathConstants<float>::sqrt2;
    constexpr int coefficientStride = 16;
    float* sendL = sendScratch.getWritePointer (0);
    float* sendR = sendScratch.getWritePointer (1);

    for (int i = 0; i < numSamples; ++i)
    {
        if (smoothedFrequency.isSmoothing())
        {
            const float f = smoothedFrequency.getNextValue();
            if (i % coefficientStride == 0)
                updateCoefficients (f);
        }
        const float split = chainSplit.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            auto& f = filters[(size_t) ch];
            float lp1, hp1, low, high, unused;
            f.first.tick (data[i], a1, a2, a3, k, lp1, hp1);
            f.low.tick (lp1, a1, a2, a3, k, low, unused);
            f.high.tick (hp1, a1, a2, a3, k, unused, high);

            const float sent = activeBand == lows ? low : high;
            const float kept = activeBand == lows ? high : low;
            (ch == 0 ? sendL : sendR)[i] = sent;
            data[i] = data[i] + split * (kept - data[i]);
        }
        if (numChannels == 1)
            sendR[i] = sendL[i];
    }

    send.process (deviceIo, sendL, sendR, numSamples, juce::roundToInt (destination->get()),
                  on && ! swapping ? gainFromDb (levelDb->get()) : 0.0f);

    if (swapping && send.getCurrentGain() <= 0.0f && chainSplit.getCurrentValue() <= 0.0f)
        activeBand = wantedBand;
}

void CrossoverProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::crossover_svg, IconData::crossover_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
