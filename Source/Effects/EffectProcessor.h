#pragma once

#include "TempoSync.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <vector>

namespace openguitarmultifx
{

/** One AudioParameterFloat an effect has opted into BPM-synced control (see
    EffectProcessor::registerTempoSyncParam()). The audio thread never sees
    this struct or knows sync exists at all -- it only ever reads the
    underlying parameter's current ms value, exactly as before; `synced`/
    `subdivisionIndex` are purely a control-thread convenience for computing
    what value to push into that parameter next (see
    EffectProcessor::updateTempoSyncedParams()). */
struct TempoSyncBinding
{
    juce::AudioParameterFloat* param = nullptr;
    bool synced = false;
    int subdivisionIndex = tempoSync::defaultSubdivisionIndex;
};

/**
    The contract every stage of the signal chain implements (see
    ARCHITECTURE.md, section C).

    process() runs exclusively on the audio thread: it never allocates,
    never blocks, never does I/O. prepare()/reset()/getState()/setState()
    run on the control thread, before the processor enters a SignalGraph
    that gets published (see Engine/DeferredReclaimer.h).
*/
class EffectProcessor
{
public:
    virtual ~EffectProcessor() = default;

    virtual void prepare (double sampleRate, int maxBlockSize, int numChannels) = 0;

    /** prepare(), unless this processor is already prepared with exactly these arguments. SignalGraph::prepare() uses it:
        every change to the chain (moving, adding or removing ONE block) builds a new graph and prepares all of it, including
        the processors that are playing right now, and a prepare() clears delay lines, reverb tails, filter and envelope
        state -- an audible pop on every block the user did not even touch (`Tests/RePrepareTransparencyTests.cpp` lists
        the ones that did it). Control thread only. */
    bool prepareIfNeeded (double sampleRate, int maxBlockSize, int numChannels)
    {
        if (isPrepared && preparedRate == sampleRate && preparedBlockSize == maxBlockSize && preparedChannels == numChannels)
            return false;

        prepare (sampleRate, maxBlockSize, numChannels);
        bypassDry.setSize (numChannels, maxBlockSize, false, true, true);
        isPrepared = true;
        preparedRate = sampleRate;
        preparedBlockSize = maxBlockSize;
        preparedChannels = numChannels;
        return true;
    }
    virtual void process (juce::AudioBuffer<float>& buffer) = 0;

    /** What SignalGraph calls instead of process(): process() plus the bypass switch. Audio thread only.

        Bypass used to be a hard cut (process() simply stopped being called), which clicks -- and snapshots
        (docs/PresetsAndSnapshots.md) toggle bypass mid-note. Now the switch is a bypassRampSeconds crossfade between
        the dry input and the processed output. A processor with hasTrails() is instead faded at its INPUT and keeps
        running while bypassed, so its repeats/reverb tail die away naturally on top of the dry signal; once that tail
        has been silent for trailsSilenceSeconds it stops being processed, so a bypassed block costs nothing again.
        A processor first heard while already bypassed starts silent, with no ramp. */
    void processWithBypass (juce::AudioBuffer<float>& buffer)
    {
        const float target = isBypassed() ? 0.0f : 1.0f;
        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        if (wetAmount < 0.0f) // first block ever heard
            wetAmount = target;

        if (wetAmount == target && target == 1.0f)
        {
            process (buffer);
            return;
        }

        const bool fits = numChannels <= bypassDry.getNumChannels() && numSamples <= bypassDry.getNumSamples();
        if (! fits) // never prepared at this size -- fall back to the old hard switch rather than allocate here
        {
            wetAmount = target;
            if (target == 1.0f)
                process (buffer);
            return;
        }

        if (wetAmount == target) // fully bypassed
        {
            if (! (hasTrails() && tailRunning))
                return;

            for (int ch = 0; ch < numChannels; ++ch)
                bypassDry.copyFrom (ch, 0, buffer, ch, 0, numSamples);
            buffer.clear();
            process (buffer);

            const bool silent = buffer.getMagnitude (0, numSamples) < trailsSilenceThreshold;
            silentSamples = silent ? silentSamples + numSamples : 0;
            if (silentSamples >= (int) (trailsSilenceSeconds * preparedRate))
                tailRunning = false;

            for (int ch = 0; ch < numChannels; ++ch)
                buffer.addFrom (ch, 0, bypassDry, ch, 0, numSamples);
            return;
        }

        // Ramping, either way.
        if (target == 0.0f && hasTrails())
        {
            tailRunning = true;
            silentSamples = 0;
        }

        const float step = (target > wetAmount ? 1.0f : -1.0f) / (float) juce::jmax (1.0, bypassRampSeconds * preparedRate);
        const float start = wetAmount;
        auto gainAt = [start, step, target] (int i)
        {
            const float g = start + step * (float) (i + 1);
            return step > 0.0f ? juce::jmin (g, target) : juce::jmax (g, target);
        };

        for (int ch = 0; ch < numChannels; ++ch)
            bypassDry.copyFrom (ch, 0, buffer, ch, 0, numSamples);

        if (hasTrails())
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* data = buffer.getWritePointer (ch);
                for (int i = 0; i < numSamples; ++i)
                    data[i] *= gainAt (i);
            }
            process (buffer);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* out = buffer.getWritePointer (ch);
                const auto* dry = bypassDry.getReadPointer (ch);
                for (int i = 0; i < numSamples; ++i)
                    out[i] += dry[i] * (1.0f - gainAt (i));
            }
        }
        else
        {
            process (buffer);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* out = buffer.getWritePointer (ch);
                const auto* dry = bypassDry.getReadPointer (ch);
                for (int i = 0; i < numSamples; ++i)
                {
                    const float g = gainAt (i);
                    out[i] = out[i] * g + dry[i] * (1.0f - g);
                }
            }
        }

        wetAmount = gainAt (numSamples - 1);
    }

    /** True for delays and reverbs: bypassing them lets the repeats/tail ring out (see processWithBypass()). */
    virtual bool hasTrails() const { return false; }

    static constexpr double bypassRampSeconds = 0.010;
    static constexpr double trailsSilenceSeconds = 0.5;
    static constexpr float trailsSilenceThreshold = 1.0e-5f; // -100 dBFS
    virtual void reset() = 0;

    void setBypassed (bool shouldBypass) noexcept { bypassed.store (shouldBypass, std::memory_order_relaxed); }
    bool isBypassed() const noexcept { return bypassed.load (std::memory_order_relaxed); }

    virtual juce::AudioProcessorParameterGroup* getParameters() = 0;

    /** The editor's pages of knobs. Page 0 is the float parameters that are direct children of getParameters(); every
        sub-group of it is a further page (its float parameters, in order). Almost every effect has one page; the
        amplifiers keep their "second page" controls (power drive, bias, ...) in a sub-group. All of them stay in
        getParameters(true), so presets, MIDI Learn and automation see every parameter regardless of the page. */
    std::vector<std::vector<juce::AudioParameterFloat*>> getParameterPages()
    {
        std::vector<std::vector<juce::AudioParameterFloat*>> pages (1);
        if (auto* group = getParameters())
            for (auto* node : *group)
            {
                if (auto* param = node->getParameter())
                {
                    if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (param))
                        pages[0].push_back (f);
                }
                else if (auto* sub = node->getGroup())
                {
                    std::vector<juce::AudioParameterFloat*> page;
                    for (auto* p : sub->getParameters (true))
                        if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                            page.push_back (f);
                    if (! page.empty())
                        pages.push_back (std::move (page));
                }
            }
        return pages;
    }

    /**
        Default implementation: walks getParameters() and serializes every
        juce::AudioParameterFloat by its paramID. Covers every processor
        whose state is fully described by its float parameters (true for
        all the Phase 1 pedals) -- override only if a processor needs more
        than that (e.g. a loaded model reference).
    */
    virtual std::unique_ptr<juce::XmlElement> getState() const
    {
        auto xml = std::make_unique<juce::XmlElement> ("EffectState");

        if (auto* group = const_cast<EffectProcessor*> (this)->getParameters())
            for (auto* param : group->getParameters (true))
                if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*> (param))
                    xml->setAttribute (floatParam->paramID, (double) floatParam->get());

        // Only written when actually synced -- an unsynced binding is
        // indistinguishable from a processor with no tempo-sync bindings at
        // all, so there's nothing worth persisting for it.
        for (auto& binding : tempoSyncBindings)
            if (binding.synced && binding.param != nullptr)
            {
                xml->setAttribute (juce::String (binding.param->paramID) + "_synced", true);
                xml->setAttribute (juce::String (binding.param->paramID) + "_subdiv", binding.subdivisionIndex);
            }

        return xml;
    }

    virtual void setState (const juce::XmlElement& state)
    {
        if (auto* group = getParameters())
            for (auto* param : group->getParameters (true))
                if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*> (param))
                    if (state.hasAttribute (floatParam->paramID))
                        *floatParam = (float) state.getDoubleAttribute (floatParam->paramID);

        for (auto& binding : tempoSyncBindings)
        {
            if (binding.param == nullptr)
                continue;

            const auto syncedKey = juce::String (binding.param->paramID) + "_synced";
            binding.synced = state.getBoolAttribute (syncedKey, false);
            binding.subdivisionIndex = state.getIntAttribute (
                juce::String (binding.param->paramID) + "_subdiv", tempoSync::defaultSubdivisionIndex);
        }
    }

    virtual const char* getName() const = 0;

    /**
        Lets generic UI code offer a "Load model..." action without
        needing to know concrete processor types (no dynamic_cast, no
        per-effect UI code). False/no-op for every processor except the
        NAM ones.
    */
    virtual bool wantsModelFile() const { return false; }
    virtual void loadModelFile (const juce::File&, int /*slotIndex*/ = 0) {}

    /** How many independent model/IR slots this processor exposes -- 1 for
        every existing single-file processor (NAM, IRLoader), >1 lets
        generic UI code (ParameterPanel) offer a slot picker before opening
        the file browser instead of assuming there's only ever one target. */
    virtual int getModelSlotCount() const { return 1; }
    /** Display name for slot `index` (0-based). Only consulted when
        getModelSlotCount() > 1. */
    virtual juce::String getModelSlotName (int /*index*/) const { return {}; }

    /** Free-form one-line status for generic UI display (e.g. "Loaded: foo.nam"). Empty if nothing to show. */
    virtual juce::String getStatusText() const { return {}; }

    /** Per-type accent colour for the chain block UI -- lets the UI tell effect
        categories apart at a glance without knowing concrete types. */
    virtual juce::Colour getAccentColour() const { return juce::Colour (0xff2d5c56); }

    /** Draws a small representative glyph into `bounds` (already positioned/sized
        by the caller). Default: nothing -- override for anything shown in the chain UI. */
    virtual void drawIcon (juce::Graphics&, juce::Rectangle<float>) const {}

    /** Which of this processor's params (if any) offer the ms/BPM-
        subdivision toggle -- ParameterPanel checks this by pointer against
        each knob it builds. Empty for the vast majority of processors. */
    const std::vector<TempoSyncBinding>& getTempoSyncBindings() const { return tempoSyncBindings; }

    void setTempoSyncEnabled (juce::AudioParameterFloat* param, bool enabled)
    {
        if (auto* binding = findTempoSyncBinding (param))
            binding->synced = enabled;
    }

    void setTempoSyncSubdivision (juce::AudioParameterFloat* param, int subdivisionIndex)
    {
        if (auto* binding = findTempoSyncBinding (param))
            binding->subdivisionIndex = juce::jlimit (0, tempoSync::numSubdivisions - 1, subdivisionIndex);
    }

    /** Control-thread only (MainComponent's UI timer, fed FooterBar's
        tap-tempo BPM). Recomputes ms from (bpm, subdivision) for every
        synced binding and pushes it into the parameter exactly like a UI-
        driven knob change would -- never touches the audio thread, which
        stays completely unaware sync mode exists. Clamped into the
        parameter's own range: a whole note at a slow BPM can exceed a
        short delay's max time, and silently misbehaving is worse than
        clamping to what the knob could reach by hand anyway. */
    void updateTempoSyncedParams (double bpm)
    {
        for (auto& binding : tempoSyncBindings)
        {
            if (! binding.synced || binding.param == nullptr)
                continue;

            const auto range = binding.param->getNormalisableRange();
            const float ms = juce::jlimit (range.start, range.end,
                tempoSync::msForSubdivision (binding.subdivisionIndex, bpm));

            if (! juce::exactlyEqual (ms, binding.param->get()))
                *binding.param = ms;
        }
    }

protected:
    /** Called by a subclass's constructor, after the parameter itself is
        constructed, to mark it as eligible for BPM-synced note-subdivision
        control instead of a raw ms drag -- e.g. a Delay's "Time" param.
        Registration order is display order for the inline toggle. */
    void registerTempoSyncParam (juce::AudioParameterFloat* param)
    {
        tempoSyncBindings.push_back ({ param, false, tempoSync::defaultSubdivisionIndex });
    }

private:
    TempoSyncBinding* findTempoSyncBinding (juce::AudioParameterFloat* param)
    {
        for (auto& binding : tempoSyncBindings)
            if (binding.param == param)
                return &binding;
        return nullptr;
    }

    std::atomic<bool> bypassed { false };
    std::vector<TempoSyncBinding> tempoSyncBindings;

    // Audio-thread state of processWithBypass(). wetAmount < 0 = not heard yet.
    juce::AudioBuffer<float> bypassDry; // sized in prepareIfNeeded(), never on the audio thread
    float wetAmount = -1.0f;
    bool tailRunning = false;
    int silentSamples = 0;

    bool isPrepared = false; // control thread only, see prepareIfNeeded()
    double preparedRate = 0.0;
    int preparedBlockSize = 0, preparedChannels = 0;
};

} // namespace openguitarmultifx
