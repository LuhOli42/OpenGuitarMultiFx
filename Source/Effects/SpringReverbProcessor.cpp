#include "SpringReverbProcessor.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

SpringReverbProcessor::SpringReverbProcessor()
{
    auto decayParam = std::make_unique<juce::AudioParameterFloat> (
        "spring_decay", "Decay", juce::NormalisableRange<float> (0.0f, 0.97f), 0.7f);
    auto toneParam = std::make_unique<juce::AudioParameterFloat> (
        "spring_tone", "Tone", juce::NormalisableRange<float> (0.0f, 1.0f), 0.4f);
    auto mixParam = std::make_unique<juce::AudioParameterFloat> (
        "spring_wet", "Wet", juce::NormalisableRange<float> (0.0f, 1.0f), 0.43f);

    decay = decayParam.get();
    tone = toneParam.get();
    mix = mixParam.get();
    auto killDryParam = std::make_unique<juce::AudioParameterFloat> (
        "spring_kill_dry", "Kill Dry", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return v > 0.5f ? juce::String ("On") : juce::String ("Off"); }));
    killDry = killDryParam.get();
    registerLegacyMix (mix, killDry, "spring_mix");

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "spring", "Spring", "|",
        std::move (decayParam), std::move (toneParam), std::move (mixParam));
    parameters->addChild (std::move (killDryParam));
}

void SpringReverbProcessor::prepare (double sampleRate, int, int)
{
    for (auto& m : wetMatch)
        m.prepare (sampleRate, 0.83f);
    currentSampleRate = sampleRate;

    // Three ascending allpass delays per channel -- the classic Schroeder
    // dispersion chain that gives the metallic, "smeared" spring timbre.
    static constexpr float allpassDelaysMs[numAllpassStages] = { 3.1f, 5.3f, 7.9f };

    for (auto& channelStages : allpass)
        for (int i = 0; i < numAllpassStages; ++i)
        {
            channelStages[(size_t) i].prepare (sampleRate, allpassDelaysMs[i]);
            channelStages[(size_t) i].feedback = 0.6f;
        }

    // ~45ms comb loop -- long enough to hear as a distinct "boing" repeat,
    // not a smooth wash (that's ReverbProcessor's job).
    for (auto& c : comb)
        c.prepare (sampleRate, 45.0f);

    smoothedDecay.reset (sampleRate, paramRampSeconds);
    smoothedDecay.setCurrentAndTargetValue (decay->get());
    smoothedTone.reset (sampleRate, paramRampSeconds);
    smoothedTone.setCurrentAndTargetValue (tone->get());
    smoothedMix.reset (sampleRate, paramRampSeconds);
    smoothedDry.reset (sampleRate, 0.02);
    smoothedMix.setCurrentAndTargetValue (mix->get());
    smoothedDry.setCurrentAndTargetValue (killDry->get() > 0.5f ? 0.0f : 1.0f);

    reset();
}

void SpringReverbProcessor::reset()
{
    for (auto& m : wetMatch)
        m.reset();
    for (auto& channelStages : allpass)
        for (auto& stage : channelStages)
            stage.clear();
    for (auto& c : comb)
        c.clear();
}

void SpringReverbProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();

    // Decay/tone only re-tune loop coefficients -- a per-block stepped
    // ramp; mix multiplies audio, so it ramps per sample instead.
    smoothedDecay.setTargetValue (decay->get());
    const float decayAmount = smoothedDecay.skip (numSamples);
    smoothedTone.setTargetValue (tone->get());
    // Freeverb-style damped comb: dampCoeff closer to 1 means the loop's
    // one-pole filter tracks the input more slowly -- i.e. darker. Tone is
    // brightness, so invert it for the damping coefficient.
    const float dampCoeff = 1.0f - smoothedTone.skip (numSamples);
    smoothedMix.setTargetValue (mix->get());
    smoothedDry.setTargetValue (killDry->get() > 0.5f ? 0.0f : 1.0f);

    // Sample-major so smoothedMix.getNextValue() advances once per sample
    // (not once per channel-sample) and both channels share the ramp.
    for (int i = 0; i < numSamples; ++i)
    {
        const float wet = smoothedMix.getNextValue();
        const float dry = smoothedDry.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            auto& stages = allpass[(size_t) ch];
            auto& c = comb[(size_t) ch];

            const float input = data[i];

            float allpassOut = input;
            for (auto& stage : stages)
                allpassOut = stage.process (allpassOut);

            const float bufferOut = c.buffer[(size_t) c.pos];
            c.lowpassState = bufferOut * (1.0f - dampCoeff) + c.lowpassState * dampCoeff;
            c.buffer[(size_t) c.pos] = allpassOut + c.lowpassState * decayAmount;
            if (++c.pos >= (int) c.buffer.size())
                c.pos = 0;

            wetMatch[(size_t) ch].accumulate (input, bufferOut);
            data[i] = input * dry + bufferOut * wet * wetMatch[(size_t) ch].gain();
        }
    }

    for (int ch = 0; ch < numChannels; ++ch)
        wetMatch[(size_t) ch].endBlock (numSamples, 1);
}

void SpringReverbProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/spring.svg -- the
    // unified icon set's "Spring" glyph (Reverb category).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::spring_svg, IconData::spring_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
