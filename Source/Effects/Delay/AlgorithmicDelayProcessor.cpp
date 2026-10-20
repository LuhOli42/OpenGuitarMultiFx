#include "AlgorithmicDelayProcessor.h"
#include "Effects/FastMath.h"
#include "Effects/IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double maxTimeSeconds = 1.5;
    constexpr double shimmerWindowSeconds = 0.06; // the pitch shifter's crossfading window

    double timeSeconds (double knob) { return 0.02 * std::pow (maxTimeSeconds / 0.02, knob); } // 20 ms .. 1.5 s
}

AlgorithmicDelayProcessor::AlgorithmicDelayProcessor (AlgorithmicDelay k) : kind (k)
{
    auto timeLabel = [] (float v, int) { return juce::String (timeSeconds (v) * 1000.0, 0) + " ms"; };
    juce::String prefix, name;
    switch (kind)
    {
        case AlgorithmicDelay::ducking: prefix = "duckdelay"; name = "Ducking Delay"; break;
        case AlgorithmicDelay::shimmer: prefix = "shimmerdelay"; name = "Shimmer Delay"; break;
        case AlgorithmicDelay::lofi: prefix = "lofidelay"; name = "Lo-Fi Delay"; break;
        case AlgorithmicDelay::granular: prefix = "granulardelay"; name = "Granular Delay"; break;
    }
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (prefix, name, "|");
    {
        auto p = std::make_unique<juce::AudioParameterFloat> (prefix + "_time", "Time", juce::NormalisableRange<float> (0.0f, 1.0f), 0.6f,
                                                              juce::AudioParameterFloatAttributes().withStringFromValueFunction (timeLabel));
        time = p.get();
        parameters->addChild (std::move (p));
    }
    feedback = add (prefix + "_feedback", "Feedback", 0.4f);
    level = add (prefix + "_level", "Level", 0.5f);
    switch (kind)
    {
        case AlgorithmicDelay::ducking:
            amount = add (prefix + "_duck", "Duck", 0.7f);
            release = add (prefix + "_release", "Release", 0.4f);
            break;
        case AlgorithmicDelay::shimmer:
            pitch = add (prefix + "_pitch", "Pitch", 0.0f, 2, { "Octave", "Fifth" });
            amount = add (prefix + "_tone", "Tone", 0.6f);
            break;
        case AlgorithmicDelay::lofi:
            amount = add (prefix + "_degrade", "Degrade", 0.5f);
            break;
        case AlgorithmicDelay::granular:
            grainSize = add (prefix + "_grain", "Grain", 0.4f);
            density = add (prefix + "_density", "Density", 0.5f);
            reverse = add (prefix + "_reverse", "Reverse", 0.3f);
            pitch = add (prefix + "_octave", "Octave", 0.2f);
            break;
    }
}

const char* AlgorithmicDelayProcessor::getName() const
{
    switch (kind)
    {
        case AlgorithmicDelay::ducking: return "Ducking Delay";
        case AlgorithmicDelay::shimmer: return "Shimmer Delay";
        case AlgorithmicDelay::lofi: return "Lo-Fi Delay";
        case AlgorithmicDelay::granular: return "Granular Delay";
    }
    return "Delay";
}

juce::AudioParameterFloat* AlgorithmicDelayProcessor::add (const juce::String& id, const juce::String& name, float def, int positions,
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

void AlgorithmicDelayProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    for (auto& ch : channels)
    {
        ch.line.prepare (newSampleRate, maxTimeSeconds * 1.2 + 0.7); // + a reversed grain's run past its start
        ch.pitchLine.prepare (newSampleRate, 2.0 * shimmerWindowSeconds + 0.01);
        ch.tone.prepare (newSampleRate, 6000.0);
    }
    lastAmount = -1.0;
    smoothedTime.reset (newSampleRate, 0.12);
    smoothedTime.setCurrentAndTargetValue ((float) timeSeconds (time->get()));
    reset();
}

void AlgorithmicDelayProcessor::reset()
{
    for (auto& ch : channels)
    {
        ch.line.reset();
        ch.pitchLine.reset();
        ch.tone.reset();
        ch.lastWet = ch.envelope = ch.held = ch.holdCount = ch.pitchPhase = 0.0;
        for (auto& g : ch.grains)
            g.active = false;
    }
    grainClock = 0.0;
}

double AlgorithmicDelayProcessor::shimmerShift (Channel& ch, double x) noexcept
{
    // Delay-line pitch shifter: two taps sweeping the window at (ratio - 1) times real time, half a window apart,
    // each faded with a sin^2 window so the jump back is never heard.
    const double ratio = pitch->get() >= 0.5f ? 1.5 : 2.0;
    const double window = shimmerWindowSeconds * sampleRate;
    ch.pitchLine.push ((float) x);
    ch.pitchPhase += (ratio - 1.0) / window;
    if (ch.pitchPhase >= 1.0)
        ch.pitchPhase -= 1.0;
    double y = 0.0;
    for (int t = 0; t < 2; ++t)
    {
        double p = ch.pitchPhase + 0.5 * t;
        if (p >= 1.0)
            p -= 1.0;
        const double w = fast::sinCycles (0.5 * p);
        y += w * w * ch.pitchLine.read (1.0 + (1.0 - p) * window);
    }
    return y;
}

void AlgorithmicDelayProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    smoothedTime.setTargetValue ((float) timeSeconds (time->get()));

    const double fb = 0.98 * feedback->get();
    const double lvl = 2.0 * level->get();
    const double amt = amount != nullptr ? amount->get() : 0.0;

    // Ducking: envelope attack 5 ms, release from the knob (50 ms .. 1.5 s).
    const double attack = 1.0 - std::exp (-1.0 / (0.005 * sampleRate));
    const double rel = release != nullptr ? 1.0 - std::exp (-1.0 / ((0.05 * std::pow (30.0, (double) release->get())) * sampleRate)) : 0.0;
    // Lo-fi: each pass through the loop is sample-held at a lower rate, quantised coarser and darkened.
    const double holdSamples = 1.0 + 7.0 * amt;
    const double bits = 12.0 - 8.0 * amt;
    const double quant = 2.0 / std::pow (2.0, bits);
    if (std::abs (amt - lastAmount) > 1.0e-6 && (kind == AlgorithmicDelay::lofi || kind == AlgorithmicDelay::shimmer))
    {
        lastAmount = amt;
        const double hz = kind == AlgorithmicDelay::lofi ? 7000.0 - 5500.0 * amt : 2000.0 + 10000.0 * amt;
        for (auto& ch : channels)
            ch.tone.retune (sampleRate, hz);
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const double delaySamples = smoothedTime.getNextValue() * sampleRate;

        for (int c = 0; c < numChannels; ++c)
        {
            auto& ch = channels[(size_t) c];
            auto* data = buffer.getWritePointer (c);
            const double x = data[i];
            double wet = 0.0;

            switch (kind)
            {
                case AlgorithmicDelay::ducking:
                {
                    ch.line.push ((float) (x + fb * ch.lastWet));
                    wet = ch.line.read (delaySamples);
                    ch.lastWet = wet;
                    const double rect = std::abs (x);
                    ch.envelope += (rect > ch.envelope ? attack : rel) * (rect - ch.envelope);
                    // Fully ducked once the playing is above ~-26 dBFS; the repeats come back as it decays.
                    const double duck = 1.0 - amt * std::min (1.0, ch.envelope / 0.05);
                    wet *= duck;
                    break;
                }
                case AlgorithmicDelay::shimmer:
                {
                    const double back = ch.tone.process (shimmerShift (ch, ch.lastWet));
                    ch.line.push ((float) (x + fb * back));
                    wet = ch.line.read (delaySamples);
                    ch.lastWet = wet;
                    break;
                }
                case AlgorithmicDelay::lofi:
                {
                    ch.line.push ((float) (x + fb * ch.lastWet));
                    double y = ch.line.read (delaySamples * (1.0 + 0.002 * amt * fast::sinCycles (grainClock)));
                    if (ch.holdCount <= 0.0)
                    {
                        ch.held = quant * std::round (y / quant);
                        ch.holdCount += holdSamples;
                    }
                    ch.holdCount -= 1.0;
                    wet = ch.tone.process (ch.held);
                    ch.lastWet = wet;
                    break;
                }
                case AlgorithmicDelay::granular:
                {
                    ch.line.push ((float) (x + fb * ch.lastWet));
                    for (auto& g : ch.grains)
                    {
                        if (! g.active)
                            continue;
                        const double phase = g.age / g.length;
                        const double w = fast::sinCycles (0.5 * phase);
                        const double pos = g.start - g.age * (g.rate - 1.0); // rate < 0 plays backwards
                        wet += w * w * ch.line.read (std::max (1.0, pos));
                        g.age += 1.0;
                        if (g.age >= g.length)
                            g.active = false;
                    }
                    wet *= 0.7;
                    ch.lastWet = wet;
                    break;
                }
            }
            data[i] = (float) (x + lvl * wet);
        }

        if (kind == AlgorithmicDelay::lofi)
        {
            grainClock += 0.8 / sampleRate;
            if (grainClock >= 1.0) grainClock -= 1.0;
        }
        else if (kind == AlgorithmicDelay::granular)
        {
            // Spawn grains: density sets how many start per second, Grain their length, Reverse / Octave the odds.
            const double grainLen = (0.02 + 0.28 * grainSize->get()) * sampleRate;
            grainClock += (2.0 + 38.0 * density->get()) / sampleRate;
            if (grainClock >= 1.0)
            {
                grainClock -= 1.0;
                const bool backwards = random.nextFloat() < reverse->get();
                const bool octave = random.nextFloat() < pitch->get();
                const double rate = (octave ? 2.0 : 1.0) * (backwards ? -1.0 : 1.0);
                const double spread = (0.3 + 0.7 * random.nextDouble()) * delaySamples;
                // A grain reading backwards (or faster) needs headroom so it never runs past the write head.
                const double start = std::max (spread, grainLen * (std::abs (rate) + 1.0) + 2.0);
                for (int c = 0; c < numChannels; ++c)
                    for (auto& g : channels[(size_t) c].grains)
                        if (! g.active)
                        {
                            g = { true, std::min (start, maxTimeSeconds * sampleRate), rate, grainLen, 0.0 };
                            break;
                        }
            }
        }
    }

    for (int c = 2; c < buffer.getNumChannels(); ++c)
        buffer.copyFrom (c, 0, buffer, 0, 0, numSamples);
}

void AlgorithmicDelayProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::digital_delay_svg, IconData::digital_delay_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
