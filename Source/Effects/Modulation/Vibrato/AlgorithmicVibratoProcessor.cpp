#include "AlgorithmicVibratoProcessor.h"
#include "Effects/FastMath.h"
#include "Effects/IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double centreMs = 6.0, maxDepthMs = 5.0;
    double triangle (double p) { return p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p; }
}

AlgorithmicVibratoProcessor::AlgorithmicVibratoProcessor (AlgorithmicVibrato k) : kind (k)
{
    juce::String prefix, name;
    switch (kind)
    {
        case AlgorithmicVibrato::tapeWarble: prefix = "warble"; name = "Tape Warble"; break;
        case AlgorithmicVibrato::stereo: prefix = "stereovib"; name = "Stereo Vibrato"; break;
        case AlgorithmicVibrato::random: prefix = "randomvib"; name = "Random Vibrato"; break;
    }
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (prefix, name, "|");
    rate = add (prefix + "_rate", "Rate", 0.4f);
    depth = add (prefix + "_depth", "Depth", 0.4f);
    switch (kind)
    {
        case AlgorithmicVibrato::tapeWarble: extra = add (prefix + "_age", "Age", 0.4f); break;
        case AlgorithmicVibrato::stereo: shape = add (prefix + "_shape", "Shape", 0.0f, 2, { "Sine", "Triangle" }); break;
        case AlgorithmicVibrato::random: extra = add (prefix + "_smooth", "Smooth", 0.5f); break;
    }
}

const char* AlgorithmicVibratoProcessor::getName() const
{
    switch (kind)
    {
        case AlgorithmicVibrato::tapeWarble: return "Tape Warble";
        case AlgorithmicVibrato::stereo: return "Stereo Vibrato";
        case AlgorithmicVibrato::random: return "Random Vibrato";
    }
    return "Vibrato";
}

juce::AudioParameterFloat* AlgorithmicVibratoProcessor::add (const juce::String& id, const juce::String& name, float def, int positions,
                                                             juce::StringArray labels)
{
    std::unique_ptr<juce::AudioParameterFloat> p;
    if (positions >= 2)
        p = std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, (float) (positions - 1), 1.0f), def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([labels] (float v, int)
            { return labels[juce::jlimit (0, labels.size() - 1, juce::roundToInt (v))]; }));
    else
        p = std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    auto* raw = p.get();
    parameters->addChild (std::move (p));
    return raw;
}

void AlgorithmicVibratoProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    for (auto& ch : channels)
    {
        ch.line.prepare (newSampleRate, (centreMs + maxDepthMs + 2.0) * 1.0e-3);
        ch.tone.prepare (newSampleRate, 16000.0);
    }
    lastExtra = -1.0;
    reset();
}

void AlgorithmicVibratoProcessor::reset()
{
    for (auto& ch : channels)
    {
        ch.line.reset();
        ch.tone.reset();
    }
    phase = flutterPhase = wow = wowTarget = rand = randTarget = 0.0;
}

void AlgorithmicVibratoProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const double hz = 0.2 * std::pow (12.0 / 0.2, (double) rate->get());
    const double depthMs = maxDepthMs * depth->get();
    const double msToSamples = 1.0e-3 * sampleRate;

    if (kind == AlgorithmicVibrato::tapeWarble && std::abs ((double) extra->get() - lastExtra) > 1.0e-6)
    {
        lastExtra = extra->get();
        for (auto& ch : channels)
            ch.tone.retune (sampleRate, 14000.0 - 10000.0 * lastExtra); // a worn tape and heads lose the top
    }
    // Random targets: a new one every cycle of Rate; glide toward it (Smooth = how slowly).
    const double glide = extra != nullptr && kind == AlgorithmicVibrato::random
                             ? 1.0 - std::exp (-1.0 / ((0.02 + 0.5 * extra->get()) / hz * sampleRate))
                             : 1.0 - std::exp (-1.0 / (0.25 / hz * sampleRate));

    for (int i = 0; i < numSamples; ++i)
    {
        const double prev = phase;
        phase += hz / sampleRate;
        if (phase >= 1.0) phase -= 1.0;
        const bool wrapped = phase < prev;

        double mod[2] { 0.0, 0.0 };
        switch (kind)
        {
            case AlgorithmicVibrato::tapeWarble:
            {
                // Wow: a wandering, not periodic, speed (new target each Rate cycle, glided); flutter: ~11 Hz, smaller.
                if (wrapped) wowTarget = random.nextDouble() * 2.0 - 1.0;
                wow += glide * (wowTarget - wow);
                flutterPhase += 11.0 / sampleRate;
                if (flutterPhase >= 1.0) flutterPhase -= 1.0;
                mod[0] = mod[1] = wow + 0.15 * (0.5 + extra->get()) * fast::sinCycles (flutterPhase);
                break;
            }
            case AlgorithmicVibrato::stereo:
            {
                const bool tri = shape->get() >= 0.5f;
                mod[0] = tri ? triangle (phase) : fast::sinCycles (phase);
                double p2 = phase + 0.5;
                if (p2 >= 1.0) p2 -= 1.0;
                mod[1] = tri ? triangle (p2) : fast::sinCycles (p2);
                break;
            }
            case AlgorithmicVibrato::random:
                if (wrapped) randTarget = random.nextDouble() * 2.0 - 1.0;
                rand += glide * (randTarget - rand);
                mod[0] = mod[1] = rand;
                break;
        }

        for (int c = 0; c < numChannels; ++c)
        {
            auto& ch = channels[(size_t) c];
            auto* data = buffer.getWritePointer (c);
            ch.line.push (data[i]);
            const double y = ch.line.read ((centreMs + depthMs * mod[c]) * msToSamples);
            data[i] = (float) (kind == AlgorithmicVibrato::tapeWarble ? ch.tone.process (y) : y);
        }
    }

    for (int c = 2; c < buffer.getNumChannels(); ++c)
        buffer.copyFrom (c, 0, buffer, 0, 0, numSamples);
}

void AlgorithmicVibratoProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::vibrato_svg, IconData::vibrato_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
