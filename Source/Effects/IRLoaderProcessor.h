#pragma once

#include "EffectProcessor.h"
#include "../Engine/DeferredReclaimer.h"

#include <juce_dsp/juce_dsp.h>

#include <atomic>

namespace openguitarmultifx
{

/**
    Loads a WAV impulse response and convolves the signal with it. Used for
    two chain roles -- "Cab" and "Reverb"/"Space" -- because they're the
    same DSP operation underneath (convolve with a fixed IR); only the
    typical IR length and chain position differ. Same one-class-two-roles
    pattern as NAMProcessor, and just as important a distinction here: this
    class is for TONE3000 items with format=ir. Nothing neural runs in
    here at all -- format=nam items go to NAMProcessor instead. Mixing
    those two up is a routing bug, not a matter of taste.

    Partitioned convolution (juce::dsp::Convolution), per ARCHITECTURE.md
    section E: direct convolution is too expensive for long IRs, and a
    single FFT block adds latency incompatible with live use.

    juce::dsp::Convolution::loadImpulseResponse() mutates the object in
    place and isn't safe to call while the audio thread might be inside
    process() on the same instance -- so, same discipline as NAMProcessor's
    nam::DSP: a fresh Convolution is built and prepared entirely on the
    control thread, then handed to the audio thread via the same
    DeferredReclaimer atomic-swap pattern.
*/
class IRLoaderProcessor : public EffectProcessor,
                           private juce::Timer
{
public:
    explicit IRLoaderProcessor (juce::String chainRoleName = "Cab");
    ~IRLoaderProcessor() override;

    /** Control thread only. */
    void loadImpulseResponse (const juce::File& irFile);
    void clearImpulseResponse();
    bool hasImpulseResponse() const noexcept { return irSlot.currentRaw() != nullptr; }
    juce::String getLoadedIRName() const { return loadedName; }

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return name.toRawUTF8(); }

    // Base EffectProcessor::getState()/setState() only knows about float
    // parameters -- which IR file is loaded is exactly the kind of "more
    // than that" state the base class docs call out. Needed for presets to
    // actually restore an IR, not just mix/output gain.
    std::unique_ptr<juce::XmlElement> getState() const override;
    void setState (const juce::XmlElement& state) override;

    // "model file" is a generic UI hook (see EffectProcessor) -- here it
    // means "IR file", not a neural model.
    bool wantsModelFile() const override { return true; }
    void loadModelFile (const juce::File& file, int /*slotIndex*/) override { loadImpulseResponse (file); }
    juce::String getStatusText() const override
    {
        return hasImpulseResponse() ? "Loaded: " + loadedName : juce::String ("No IR loaded");
    }

    juce::Colour getAccentColour() const override;
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    void timerCallback() override { irSlot.sweep(); }
    bool isReverbRole() const noexcept { return name.containsIgnoreCase ("Reverb") || name.containsIgnoreCase ("Space"); }

    juce::String name;
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* mixParam = nullptr;
    juce::AudioParameterFloat* outputGainDb = nullptr;

    DeferredReclaimer<juce::dsp::Convolution> irSlot;
    juce::File lastLoadedFile;
    juce::String loadedName;

    double sampleRate = 0.0;
    int preparedBlockSize = 0;
    int preparedNumChannels = 2;

    // Pre-allocated in prepare() for the dry/wet mix -- process() must never allocate.
    juce::AudioBuffer<float> dryScratch;
};

} // namespace openguitarmultifx
