#pragma once

#include "SynthParts.h"
#include "../MultiType/MultiTypeIcons.h"

namespace openguitarmultifx
{

/** Shared shell of the guitar-synth blocks: one flat parameter page, Mix/Level, name, glyph. */
class SynthEffect : public EffectProcessor
{
public:
    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return displayName; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffb5527a); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override { mt::drawIconByKey (g, b, iconKey); }

protected:
    SynthEffect (const char* name, const char* icon, const char* groupId)
        : displayName (name), iconKey (icon)
    {
        parameters = std::make_unique<juce::AudioProcessorParameterGroup> (groupId, name, "|");
    }

    juce::AudioParameterFloat* addFloat (const juce::String& id, const juce::String& name, float lo, float hi, float def,
                                         float skew = 1.0f, const juce::String& unit = {}, int decimals = 1)
    {
        auto p = mt::floatParam (id, name, lo, hi, def, skew, unit, decimals);
        auto* r = p.get();
        parameters->addChild (std::move (p));
        return r;
    }
    juce::AudioParameterFloat* addChoice (const juce::String& id, const juce::String& name, const juce::StringArray& names, int def)
    {
        auto p = mt::choiceParam (id, name, names, def);
        auto* r = p.get();
        parameters->addChild (std::move (p));
        return r;
    }
    void addMixLevel (float mixDefault)
    {
        mix = addFloat ("mix", "Mix", 0, 100, mixDefault, 1.0f, "%", 0);
        level = addFloat ("level", "Level", -30, 12, 0, 1.0f, "dB");
    }
    /** Per-sample dry/wet and output gain (advance once per sample, then apply to every channel). */
    struct Gains
    {
        double mix, level;
        double apply (double dry, double wet) const { return (dry * (1.0 - mix) + wet * mix) * level; }
    };
    Gains nextGains() { return { (double) mixLevel.mix.getNextValue(), (double) mixLevel.level.getNextValue() }; }
    void updateMix() { mixLevel.setTargets (mix->get(), level->get()); }

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat *mix = nullptr, *level = nullptr;
    mt::MixLevel mixLevel;
    const char* displayName;
    const char* iconKey;
};

} // namespace openguitarmultifx
