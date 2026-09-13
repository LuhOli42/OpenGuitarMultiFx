#pragma once

#include "EffectProcessor.h"
#include "../Engine/DeferredReclaimer.h"

#include <NAM/dsp.h>

#include <filesystem>
#include <vector>

namespace openguitarmultifx
{

/**
    Realtime-safe wrapper over NeuralAmpModelerCore (nam::DSP). The same
    class fills three different chain roles -- "Neural Amp", "Neural Amp +
    Cab", and "Neural Pedal" -- the inference engine itself doesn't know or
    care which; only the trained .nam file loaded into it differs (a
    TONE3000 gear=amp capture vs. gear=amp-cab vs. gear=pedal -- see
    GearRouting.h). "Neural Amp" and "Neural Amp + Cab" process IDENTICALLY
    (an amp-cab capture is still just a .nam file to nam::get_dsp()) --
    they're split into two blocks purely so TONE3000 search stays scoped to
    one gear at a time, on request: an amp-only capture (pair with a
    separate Cab block yourself) and an all-in-one amp+cab capture are
    different things to go looking for, even though this engine loads
    either into the exact same code path. See
    EffectRegistry::registerBuiltInEffects for how the three roles get
    registered under different names.

    Model loading (nam::get_dsp(), which allocates) always happens on the
    control thread, inside loadModel(). The freshly built model is handed
    to the audio thread through the same DeferredReclaimer pattern used for
    the SignalGraph itself: an atomic pointer swap, with the previous model
    freed later by this object's own Timer, never on the audio thread.
*/
class NAMProcessor : public EffectProcessor,
                      private juce::Timer
{
public:
    explicit NAMProcessor (juce::String chainRoleName = "Neural Amp");
    ~NAMProcessor() override;

    /** Control thread only. Throws nam::NamFileValidationError on a bad/malformed file. */
    void loadModel (const std::filesystem::path& namFilePath);
    void clearModel();
    bool hasModel() const noexcept { return modelSlot.currentRaw() != nullptr; }
    juce::String getLoadedModelName() const;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return name.toRawUTF8(); }

    // Base EffectProcessor::getState()/setState() only knows about float
    // parameters -- which .nam file is loaded is exactly the kind of
    // "more than that" state the base class docs call out. Needed for
    // presets to actually restore a model, not just input/output gain.
    std::unique_ptr<juce::XmlElement> getState() const override;
    void setState (const juce::XmlElement& state) override;

    bool wantsModelFile() const override { return true; }
    void loadModelFile (const juce::File& file, int /*slotIndex*/) override
    {
        loadModel (std::filesystem::path (file.getFullPathName().toStdString()));
    }
    juce::String getStatusText() const override
    {
        return hasModel() ? "Loaded: " + getLoadedModelName() : juce::String ("No model loaded");
    }

    // Same wrapper, three roles -- colour/icon follow which role this
    // instance was constructed as (see the chainRoleName passed in by
    // EffectRegistry). Amp and Amp+Cab share the "Amplificadores" category
    // colour (they're the same category on the reference icon sheet); Pedal
    // gets its own.
    juce::Colour getAccentColour() const override
    {
        return isPedalRole() ? juce::Colour (0xffa8322a) : juce::Colour (0xff2f8f6e);
    }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    bool isPedalRole() const noexcept { return name.containsIgnoreCase ("Pedal"); }
    bool isAmpCabRole() const noexcept { return name.containsIgnoreCase ("Cab"); }
    void timerCallback() override { modelSlot.sweep(); }

    juce::String name;
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* inputGainDb = nullptr;
    juce::AudioParameterFloat* outputGainDb = nullptr;

    DeferredReclaimer<nam::DSP> modelSlot;
    std::filesystem::path lastLoadedPath;

    double sampleRate = 0.0;
    int preparedBlockSize = 0;

    // Fixed-size scratch for NAM's NAM_SAMPLE**-per-channel API -- sized in
    // prepare(), never touched again on the audio thread. The guitar path
    // is mono going in (see AudioEngine); NAM models are inherently mono
    // nonlinear transforms, so we sum channel 0 in and broadcast back out.
    std::vector<float> inputScratch, outputScratch;
};

} // namespace openguitarmultifx
