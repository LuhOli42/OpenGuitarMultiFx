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

    int stepOf (const juce::AudioParameterFloat* p, int numSteps) noexcept
    {
        return juce::jlimit (0, numSteps - 1, juce::roundToInt (p->get()));
    }

    std::unique_ptr<juce::AudioParameterFloat> makeBandParam (const juce::String& id)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            id, "Crossover", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            {
                const int b = juce::roundToInt (v);
                return b == LinkwitzRileySplit::lows ? juce::String ("Lows")
                     : b == LinkwitzRileySplit::highs ? juce::String ("Highs") : juce::String ("Off");
            }));
    }

    std::unique_ptr<juce::AudioParameterFloat> makeFrequencyParam (const juce::String& id)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            id, "Freq", juce::NormalisableRange<float> (40.0f, 5000.0f, 1.0f, 0.3f), 120.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            {
                return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
            }));
    }
}

juce::String DeviceIoChoices::outputName (int choice)
{
    if (const int line = outputLine (choice); line >= 0)
        return "Line " + juce::String (line + 1);
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
    if (const int line = inputLine (choice); line >= 0)
        return "Line " + juce::String (line + 1);
    const int first = inputFirstChannel (choice) + 1;
    return inputIsPair (choice) ? "In " + juce::String (first) + "/" + juce::String (first + 1)
                                : "In " + juce::String (first);
}

// ---------------------------------------------------------------------------------------------------------------------

void LinkwitzRileySplit::prepare (double newSampleRate, float frequencyHz)
{
    sampleRate = newSampleRate;
    frequency.reset (sampleRate, 0.030);
    frequency.setCurrentAndTargetValue (frequencyHz);
    updateCoefficients (frequencyHz);
    reset();
}

void LinkwitzRileySplit::reset() noexcept
{
    channels = {};
    sinceUpdate = 0;
}

void LinkwitzRileySplit::updateCoefficients (float hz) noexcept
{
    const float fc = juce::jlimit (10.0f, (float) (0.45 * sampleRate), hz);
    const float g = std::tan (juce::MathConstants<float>::pi * fc / (float) sampleRate);
    constexpr float k = juce::MathConstants<float>::sqrt2;   // 1/Q, Q = 1/sqrt(2): Butterworth
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

void LinkwitzRileySplit::tick() noexcept
{
    constexpr int coefficientStride = 16;
    if (! frequency.isSmoothing())
        return;
    const float f = frequency.getNextValue();
    if (++sinceUpdate >= coefficientStride || ! frequency.isSmoothing())
    {
        sinceUpdate = 0;
        updateCoefficients (f);
    }
}

void LinkwitzRileySplit::split (int channel, float x, float& low, float& high) noexcept
{
    constexpr float k = juce::MathConstants<float>::sqrt2;
    auto& c = channels[(size_t) juce::jlimit (0, 1, channel)];
    float lp1, hp1, unused;
    c.first.tick (x, a1, a2, a3, k, lp1, hp1);
    c.low.tick (lp1, a1, a2, a3, k, low, unused);
    c.high.tick (hp1, a1, a2, a3, k, unused, high);
}

void LinkwitzRileySplit::select (int channel, float x, int band, float& picked, float& other) noexcept
{
    float low, high;
    split (channel, x, low, high);   // runs even when off, so switching a band on starts from a settled state
    if (band == lows)        { picked = low;  other = high; }
    else if (band == highs)  { picked = high; other = low; }
    else                     { picked = x;    other = x; }
}

// ---------------------------------------------------------------------------------------------------------------------

void DeviceOutputSend::prepare (double sampleRate)
{
    gain.reset (sampleRate, rampSeconds);
    started = false;
}

void DeviceOutputSend::process (EffectProcessor::DeviceIo* io, const float* inL, const float* inR, int numSamples,
                                int key, float targetGain, bool maySwitch) noexcept
{
    if (! started)
    {
        started = true;
        activeKey = key;
        gain.setCurrentAndTargetValue (targetGain);
    }

    // A new destination (or band): fade the old one out first, switch once silent (end of block), fade the new one in.
    gain.setTargetValue (key == activeKey ? targetGain : 0.0f);

    const int destination = juce::jlimit (0, DeviceIoChoices::numOutputDestinations - 1,
                                          activeKey % DeviceIoChoices::numOutputDestinations);
    const int line = DeviceIoChoices::outputLine (destination);
    float* left = nullptr;
    float* right = nullptr;
    int mode = 0;   // 0 stereo, 1 / 2 one device channel (mono fold), 3 a one-channel line
    uint32_t outputBits = 0;

    if (io != nullptr && line >= 0)
    {
        // Forward-only: SignalGraph leaves lineSends null for this lane and every earlier one.
        auto* bus = io->lineSends[(size_t) line];
        if (bus != nullptr && bus->getNumChannels() > 0 && numSamples <= bus->getNumSamples())
        {
            left = bus->getWritePointer (0);
            right = bus->getWritePointer (juce::jmin (1, bus->getNumChannels() - 1));
            if (bus->getNumChannels() == 1)
                mode = 3;
        }
    }
    else if (io != nullptr && line < 0 && io->outputs != nullptr)
    {
        const int pairStart = DeviceIoChoices::outputPairStart (destination);
        mode = DeviceIoChoices::outputChannels (destination);
        if (pairStart + 1 < io->outputs->getNumChannels() && numSamples <= io->outputs->getNumSamples())
        {
            left = io->outputs->getWritePointer (pairStart);
            right = io->outputs->getWritePointer (pairStart + 1);
            outputBits = mode == 1 ? (1u << pairStart) : mode == 2 ? (1u << (pairStart + 1)) : (3u << pairStart);
        }
    }

    bool sent = false;
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = gain.getNextValue();
        if (left != nullptr && g > 0.0f)
        {
            sent = true;
            switch (mode)
            {
                case 1:
                case 3:  left[i] += 0.5f * g * (inL[i] + inR[i]); break;   // one channel: the signal folded to mono
                case 2:  right[i] += 0.5f * g * (inL[i] + inR[i]); break;
                default: left[i] += g * inL[i]; right[i] += g * inR[i]; break;
            }
        }
    }

    if (sent)
    {
        if (line >= 0)
            io->lineSendMask |= 1u << line;
        else
            io->outputMask |= outputBits;
    }

    if (key != activeKey && maySwitch && gain.getCurrentValue() <= 0.0f)
        activeKey = key;
}

// ---------------------------------------------------------------------------------------------------------------------

OutputSendProcessor::OutputSendProcessor()
{
    auto destParam = std::make_unique<juce::AudioParameterFloat> (
        "outsend_dest", "Output", juce::NormalisableRange<float> (0.0f, (float) DeviceIoChoices::numOutputDestinations - 1.0f, 1.0f), 3.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return DeviceIoChoices::outputName (juce::jlimit (0, DeviceIoChoices::numOutputDestinations - 1, juce::roundToInt (v)));
        }));
    auto levelParam = std::make_unique<juce::AudioParameterFloat> (
        "outsend_level", "Level", juce::NormalisableRange<float> (silenceDb, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return dbText (v); }));
    // Kept the "outsend_mute_thru" id: 0 / 1 mean what Mute Thru Off / On meant, 2 is new.
    auto thruParam = std::make_unique<juce::AudioParameterFloat> (
        "outsend_mute_thru", "Thru", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), (float) thruFull,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            const int t = juce::roundToInt (v);
            return t == thruMute ? juce::String ("Mute") : t == thruOtherBand ? juce::String ("Other band") : juce::String ("Full");
        }));
    auto bandParam = makeBandParam ("outsend_band");
    auto freqParam = makeFrequencyParam ("outsend_freq");

    destination = destParam.get();
    levelDb = levelParam.get();
    thru = thruParam.get();
    band = bandParam.get();
    frequency = freqParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("outsend", "Output Send", "|", std::move (destParam),
                                                                         std::move (bandParam), std::move (freqParam),
                                                                         std::move (thruParam), std::move (levelParam));
}

bool OutputSendProcessor::isParameterActive (const juce::AudioParameterFloat& p) const
{
    return &p != frequency || stepOf (band, 3) != LinkwitzRileySplit::full;
}

void OutputSendProcessor::prepare (double sampleRate, int maxBlockSize, int)
{
    send.prepare (sampleRate);
    crossover.prepare (sampleRate, frequency->get());
    sendScratch.setSize (2, juce::jmax (1, maxBlockSize), false, false, true);
    thruGain.reset (sampleRate, rampSeconds);
    otherBandMix.reset (sampleRate, rampSeconds);
    reset();
}

void OutputSendProcessor::reset()
{
    send.reset();
    crossover.reset();
    started = false;
}

void OutputSendProcessor::process (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (2, buffer.getNumChannels());
    if (numChannels <= 0 || numSamples > sendScratch.getNumSamples())
        return;

    constexpr int numDest = DeviceIoChoices::numOutputDestinations;
    const bool on = ! isBypassed(); // still called during the bypass crossfade: fade the send out with it
    const int thruMode = stepOf (thru, 3);
    const int wantedBand = stepOf (band, 3);
    const int key = stepOf (destination, numDest) + numDest * wantedBand;

    // The send and the chain both follow the band now PLAYING; a band change waits until both have faded out.
    const int playingBand = send.getActiveKey (key) / numDest;
    const bool bandChanging = wantedBand != playingBand;
    const float thruTarget = thruMode == thruMute && on ? 0.0f : 1.0f;
    const float otherTarget = on && thruMode == thruOtherBand && ! bandChanging ? 1.0f : 0.0f;

    if (! started)
    {
        started = true;
        thruGain.setCurrentAndTargetValue (thruTarget);
        otherBandMix.setCurrentAndTargetValue (otherTarget);
    }
    thruGain.setTargetValue (thruTarget);
    otherBandMix.setTargetValue (otherTarget);
    crossover.setTargetFrequency (frequency->get());

    float* sendL = sendScratch.getWritePointer (0);
    float* sendR = sendScratch.getWritePointer (1);
    for (int i = 0; i < numSamples; ++i)
    {
        crossover.tick();
        const float t = thruGain.getNextValue();
        const float o = otherBandMix.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            float picked, other;
            crossover.select (ch, data[i], playingBand, picked, other);
            (ch == 0 ? sendL : sendR)[i] = picked;
            data[i] = t * (data[i] + o * (other - data[i]));
        }
        if (numChannels == 1)
            sendR[i] = sendL[i];
    }

    send.process (deviceIo, sendL, sendR, numSamples, key, on ? gainFromDb (levelDb->get()) : 0.0f,
                  otherBandMix.getCurrentValue() <= 0.0f);
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
        "insource_src", "Input", juce::NormalisableRange<float> (0.0f, (float) DeviceIoChoices::numInputSources - 1.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return DeviceIoChoices::inputName (juce::jlimit (0, DeviceIoChoices::numInputSources - 1, juce::roundToInt (v)));
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
    auto bandParam = makeBandParam ("insource_band");
    auto freqParam = makeFrequencyParam ("insource_freq");

    source = srcParam.get();
    levelDb = levelParam.get();
    mix = mixParam.get();
    band = bandParam.get();
    frequency = freqParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("insource", "Input Source", "|", std::move (srcParam),
                                                                         std::move (bandParam), std::move (freqParam),
                                                                         std::move (levelParam), std::move (mixParam));
}

bool InputSourceProcessor::isParameterActive (const juce::AudioParameterFloat& p) const
{
    return &p != frequency || stepOf (band, 3) != LinkwitzRileySplit::full;
}

void InputSourceProcessor::prepare (double sampleRate, int, int)
{
    inputGain.reset (sampleRate, rampSeconds);
    chainGain.reset (sampleRate, rampSeconds);
    crossover.prepare (sampleRate, frequency->get());
    reset();
}

void InputSourceProcessor::reset()
{
    crossover.reset();
    started = false;
}

void InputSourceProcessor::process (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (2, buffer.getNumChannels());
    if (numChannels <= 0)
        return;

    constexpr int numSources = DeviceIoChoices::numInputSources;
    const int key = stepOf (source, numSources) + numSources * stepOf (band, 3);
    const bool on = ! isBypassed();
    const float m = mix->get();
    const float level = gainFromDb (levelDb->get()) * InputSourceProcessor::inputGainForMix (m);

    if (! started)
    {
        started = true;
        activeKey = key;
        inputGain.setCurrentAndTargetValue (on ? level : 0.0f);
        chainGain.setCurrentAndTargetValue (chainGainForMix (m));
    }

    inputGain.setTargetValue (on && key == activeKey ? level : 0.0f);
    chainGain.setTargetValue (chainGainForMix (m));
    crossover.setTargetFrequency (frequency->get());

    const int activeSource = activeKey % numSources;
    const int activeBand = activeKey / numSources;
    const float* a = nullptr;
    const float* b = nullptr;
    float sourceGain = 1.0f;
    if (deviceIo != nullptr)
    {
        if (const int line = DeviceIoChoices::inputLine (activeSource); line >= 0)
        {
            // Forward-only: SignalGraph leaves lineOutputs null for this lane and every later one.
            if (const auto* from = deviceIo->lineOutputs[(size_t) line];
                from != nullptr && from->getNumChannels() > 0 && numSamples <= from->getNumSamples())
            {
                a = from->getReadPointer (0);
                b = from->getReadPointer (juce::jmin (1, from->getNumChannels() - 1));
            }
        }
        else if (deviceIo->inputs != nullptr)
        {
            const int first = DeviceIoChoices::inputFirstChannel (activeSource);
            const int second = DeviceIoChoices::inputIsPair (activeSource) ? first + 1 : first;
            if (first < deviceIo->numInputs)
            {
                a = deviceIo->inputs[first];
                b = second < deviceIo->numInputs ? deviceIo->inputs[second] : a;
            }
            sourceGain = deviceIo->inputGain;
        }
    }

    for (int i = 0; i < numSamples; ++i)
    {
        crossover.tick();
        const float c = chainGain.getNextValue();
        const float g = inputGain.getNextValue() * sourceGain;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float in = 0.0f;
            if (a != nullptr)
                in = numChannels == 1 ? 0.5f * (a[i] + b[i]) : (ch == 0 ? a[i] : b[i]);
            float picked, other;
            crossover.select (ch, in, activeBand, picked, other);
            auto* data = buffer.getWritePointer (ch);
            data[i] = data[i] * c + picked * g;
        }
    }

    if (key != activeKey && inputGain.getCurrentValue() <= 0.0f)
        activeKey = key;
}

void InputSourceProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::input_source_svg, IconData::input_source_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
