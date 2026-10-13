#include "AlgorithmicPhaserProcessor.h"
#include "Effects/FastMath.h"
#include "Effects/IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double lowHz = 80.0, highHz = 5000.0;
    double cornerFor (double u) { return lowHz * std::pow (highHz / lowHz, std::clamp (u, 0.0, 1.0)); } // exponential sweep
    constexpr double capF = 10.0e-9;
    double ohmsFor (double hz) { return 1.0 / (2.0 * juce::MathConstants<double>::pi * hz * capF); }
}

double AlgorithmicPhaserProcessor::Chain::run (double x, double cornerHz, int n, double fb, double sr) noexcept
{
    double y = x + fb * lastOut;
    const double r = ohmsFor (cornerHz);
    for (int k = 0; k < n; ++k)
        y = stages[(size_t) k].process (y, r, capF, sr);
    lastOut = y;
    return y;
}

void AlgorithmicPhaserProcessor::Chain::reset() noexcept
{
    for (auto& s : stages)
        s.reset();
    lastOut = 0.0;
}

AlgorithmicPhaserProcessor::AlgorithmicPhaserProcessor (AlgorithmicPhaser k) : kind (k)
{
    juce::String prefix, name;
    switch (kind)
    {
        case AlgorithmicPhaser::barberpole: prefix = "barberphaser"; name = "Barberpole Phaser"; break;
        case AlgorithmicPhaser::envelope: prefix = "envphaser"; name = "Envelope Phaser"; break;
        case AlgorithmicPhaser::quadrature: prefix = "quadphaser"; name = "Quadrature Phaser"; break;
        case AlgorithmicPhaser::step: prefix = "stepphaser"; name = "Step Phaser"; break;
    }
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (prefix, name, "|");
    if (kind == AlgorithmicPhaser::envelope)
        sensitivity = add (prefix + "_sensitivity", "Sensitivity", 0.5f);
    else
        rate = add (prefix + "_rate", "Rate", 0.4f);
    depth = add (prefix + "_depth", "Depth", 0.8f);
    feedback = add (prefix + "_feedback", "Feedback", 0.3f);
    mix = add (prefix + "_mix", "Mix", 0.5f);
    if (kind == AlgorithmicPhaser::barberpole || kind == AlgorithmicPhaser::envelope)
        direction = add (prefix + "_direction", "Direction", 0.0f, 2, { "Up", "Down" });
    if (kind == AlgorithmicPhaser::step)
        smooth = add (prefix + "_glide", "Glide", 0.2f);
}

const char* AlgorithmicPhaserProcessor::getName() const
{
    switch (kind)
    {
        case AlgorithmicPhaser::barberpole: return "Barberpole Phaser";
        case AlgorithmicPhaser::envelope: return "Envelope Phaser";
        case AlgorithmicPhaser::quadrature: return "Quadrature Phaser";
        case AlgorithmicPhaser::step: return "Step Phaser";
    }
    return "Phaser";
}

juce::AudioParameterFloat* AlgorithmicPhaserProcessor::add (const juce::String& id, const juce::String& name, float def, int positions,
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

void AlgorithmicPhaserProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    smoothedMix.reset (newSampleRate, paramRampSeconds);
    smoothedMix.setCurrentAndTargetValue (mix->get());
    reset();
}

void AlgorithmicPhaserProcessor::reset()
{
    for (auto& ch : channels)
    {
        for (auto& c : ch.chains)
            c.reset();
        ch.envelope = 0.0;
    }
    phase = held = glided = 0.0;
}

void AlgorithmicPhaserProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    smoothedMix.setTargetValue (mix->get());

    const double dep = depth->get();
    const double fb = 0.85 * feedback->get();
    const bool down = direction != nullptr && direction->get() >= 0.5f;
    const double hz = rate != nullptr ? 0.03 * std::pow (8.0 / 0.03, (double) rate->get()) : 0.0;
    const double attack = 1.0 - std::exp (-1.0 / (0.004 * sampleRate)), release = 1.0 - std::exp (-1.0 / (0.15 * sampleRate));
    const double sens = sensitivity != nullptr ? std::pow (10.0, 2.0 * sensitivity->get()) : 1.0;
    const double glide = smooth != nullptr ? 1.0 - std::exp (-1.0 / ((0.001 + 0.2 * smooth->get()) * sampleRate)) : 1.0;

    for (int i = 0; i < numSamples; ++i)
    {
        const double m = smoothedMix.getNextValue();
        const double prevPhase = phase;
        phase += hz / sampleRate;
        if (phase >= 1.0) phase -= 1.0;
        if (kind == AlgorithmicPhaser::step && phase < prevPhase + 1.0e-12 && hz > 0.0)
            held = random.nextDouble(); // a new step each LFO cycle wrap
        if (kind == AlgorithmicPhaser::step)
            glided += glide * (held - glided);

        for (int c = 0; c < numChannels; ++c)
        {
            auto& ch = channels[(size_t) c];
            auto* data = buffer.getWritePointer (c);
            const double x = data[i];
            double wet = 0.0;

            switch (kind)
            {
                case AlgorithmicPhaser::barberpole:
                {
                    // Three 6-stage phasers ramping up (or down) a third of a cycle apart across the whole sweep, each
                    // faded in and out (sin^2, summing to one): the notches rise forever, none ever seen leaving.
                    for (int t = 0; t < 3; ++t)
                    {
                        double p = phase + t / 3.0;
                        if (p >= 1.0) p -= 1.0;
                        const double u = (down ? 1.0 - p : p) * dep + 0.5 * (1.0 - dep);
                        const double w = fast::sinCycles (0.5 * p);
                        // 2/3 makes the sin^2 weights sum to one; x1.5 (calibrated on MixLawTests' signal): three chains
                        // with notches in different places add closer to energy than to amplitude.
                        wet += 1.5 * (2.0 / 3.0) * w * w * ch.chains[(size_t) t].run (x, cornerFor (u), 6, fb, sampleRate);
                    }
                    break;
                }
                case AlgorithmicPhaser::envelope:
                {
                    const double rect = std::abs (x) * sens;
                    ch.envelope += (rect > ch.envelope ? attack : release) * (rect - ch.envelope);
                    const double e = std::min (1.0, ch.envelope);
                    const double u = (down ? 1.0 - e : e) * dep;
                    wet = ch.chains[0].run (x, cornerFor (u), 6, fb, sampleRate);
                    break;
                }
                case AlgorithmicPhaser::quadrature:
                {
                    const double lfo = fast::sinCycles (phase + (c == 1 ? 0.25 : 0.0)); // right a quarter cycle on
                    wet = ch.chains[0].run (x, cornerFor (0.5 + 0.5 * dep * lfo), 8, fb, sampleRate);
                    break;
                }
                case AlgorithmicPhaser::step:
                    wet = ch.chains[0].run (x, cornerFor (0.5 + dep * (glided - 0.5)), 6, fb, sampleRate);
                    break;
            }
            data[i] = (float) ((1.0 - m) * x + m * wet);
        }
    }

    for (int c = 2; c < buffer.getNumChannels(); ++c)
        buffer.copyFrom (c, 0, buffer, 0, 0, numSamples);
}

void AlgorithmicPhaserProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::phaser_svg, IconData::phaser_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
