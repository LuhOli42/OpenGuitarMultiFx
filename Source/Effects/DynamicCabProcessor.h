#pragma once

#include "EffectProcessor.h"
#include "EnvelopeFollower.h"
#include "../Engine/DeferredReclaimer.h"

#include <juce_dsp/juce_dsp.h>

namespace openguitarmultifx
{

/**
    Two independently-loaded cab IRs, convolved in parallel and crossfaded
    into one output -- covers two related, user-requested ideas at once
    rather than as two separate processors:

    - Static multi-mic blending: load a close-mic IR into slot A and a
      room/ribbon-mic IR into slot B, and the Blend knob mixes them like a
      real multi-mic cab recording (Dynamics at 0 -- a fixed blend, nothing
      moves).
    - "Dynamic"/level-dependent convolution: Dynamics > 0 lets how hard
      you're playing (tracked via EnvelopeFollower on the input) push the
      blend toward IR B on louder transients, the way real speaker/cab
      response brightens and compresses under harder playing -- this is a
      real, documented technique (commercial precedent: Two Notes' DynIR)
      but no open-source reference implementation exists to copy from (see
      the research this was built from), so the level-to-blend mapping
      here is this project's own simple, tunable design, not a port of
      anyone else's algorithm.

    With only one slot loaded, this behaves exactly like a single IR
    (IRLoaderProcessor's "Cab" role) -- Blend/Dynamics are ignored until a
    second IR is actually loaded, so it never goes silent or does anything
    surprising for the common single-IR case.

    Same realtime-safety discipline as IRLoaderProcessor: each
    juce::dsp::Convolution is built and prepared entirely on the control
    thread, then handed to the audio thread via DeferredReclaimer's atomic
    swap -- never mutated in place while the audio thread might be using it.
*/
class DynamicCabProcessor : public EffectProcessor,
                             private juce::Timer
{
public:
    DynamicCabProcessor();
    ~DynamicCabProcessor() override;

    /** Control thread only. slot: 0 = IR A, 1 = IR B. */
    void loadImpulseResponse (const juce::File& irFile, int slot);
    void clearImpulseResponse (int slot);
    bool hasImpulseResponse (int slot) const noexcept;
    juce::String getLoadedIRName (int slot) const { return slot == 0 ? loadedNameA : loadedNameB; }

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Dynamic Cab"; }

    std::unique_ptr<juce::XmlElement> getState() const override;
    void setState (const juce::XmlElement& state) override;

    bool wantsModelFile() const override { return true; }
    void loadModelFile (const juce::File& file, int slotIndex) override { loadImpulseResponse (file, slotIndex); }
    int getModelSlotCount() const override { return 2; }
    juce::String getModelSlotName (int index) const override { return index == 0 ? "IR A" : "IR B"; }

    juce::String getStatusText() const override;

    juce::Colour getAccentColour() const override { return juce::Colour (0xff7a5c2d); } // same category colour as Cab
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    void timerCallback() override
    {
        irSlotA.sweep();
        irSlotB.sweep();
    }

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* blend = nullptr;
    juce::AudioParameterFloat* dynamicsAmount = nullptr;
    juce::AudioParameterFloat* mixParam = nullptr;
    juce::AudioParameterFloat* outputGainDb = nullptr;

    DeferredReclaimer<juce::dsp::Convolution> irSlotA, irSlotB;
    juce::File lastLoadedFileA, lastLoadedFileB;
    juce::String loadedNameA, loadedNameB;

    EnvelopeFollower inputEnvelope;

    double sampleRate = 0.0;
    int preparedBlockSize = 0;
    int preparedNumChannels = 2;

    // Pre-allocated in prepare() -- process() must never allocate. Each
    // holds one IR's convolved output before the two get crossfaded; sized
    // at the block's max, addressed with the block's real numSamples via
    // an explicit-length juce::dsp::AudioBlock (never resized per-call).
    juce::AudioBuffer<float> scratchA, scratchB, dryScratch;
};

} // namespace openguitarmultifx
