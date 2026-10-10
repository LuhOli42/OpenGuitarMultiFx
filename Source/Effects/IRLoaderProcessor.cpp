#include "IRLoaderProcessor.h"
#include "IconKit.h"

#include <IconData.h>
#include <IRData.h>

#include <cmath>

namespace openguitarmultifx
{

namespace
{
    struct BundledIRAsset { const char* name; const char* bytes; int byteCount; };

    // Order is the "IR Source" parameter's option order -- default selection
    // is entry 0, the general-purpose closed-back.
    const BundledIRAsset bundledIRAssets[] = {
        { "4x12 Closed-Back", IRData::cab4x12closed_wav, IRData::cab4x12closed_wavSize },
        { "2x12 Open-Back",   IRData::cab2x12open_wav,   IRData::cab2x12open_wavSize },
        { "1x12 Open-Back",   IRData::cab1x12open_wav,   IRData::cab1x12open_wavSize },
    };
}

namespace bundledCabIRs
{
    int count() { return (int) std::size (bundledIRAssets); }
    juce::String displayName (int index) { return bundledIRAssets[index].name; }
    juce::String selectorName (int index) { return "Built-in: " + juce::String (bundledIRAssets[index].name); }
    const char* data (int index) { return bundledIRAssets[index].bytes; }
    int dataSize (int index) { return bundledIRAssets[index].byteCount; }
}

IRLoaderProcessor::IRLoaderProcessor (juce::String chainRoleName)
    : name (std::move (chainRoleName))
{
    auto mix = std::make_unique<juce::AudioParameterFloat> (
        "ir_mix", "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f);
    auto output = std::make_unique<juce::AudioParameterFloat> (
        "ir_output", "Output", juce::NormalisableRange<float> (-24.0f, 24.0f), 0.0f);

    mixParam = mix.get();
    outputGainDb = output.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "ir", name, "|", std::move (mix), std::move (output));

    // Appended AFTER mix/output so existing param order (and presets) are
    // untouched -- see the "don't reorder parameter pages" rule. Cab role
    // only: bundled IRs are all guitar cabinets, meaningless for the reverb
    // role's space IRs.
    if (! isReverbRole())
    {
        auto source = std::make_unique<juce::AudioParameterFloat> (
            "ir_source", "IR Source",
            juce::NormalisableRange<float> (0.0f, (float) bundledCabIRs::count(), 1.0f),
            1.0f, // a fresh Cab block defaults to the first bundled IR, so amps sound like amps out of the box
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) -> juce::String
                {
                    const int i = (int) std::lround (v);
                    return i <= 0 ? juce::String ("File") : bundledCabIRs::selectorName (i - 1);
                })
                .withValueFromStringFunction ([] (const juce::String& text) -> float
                {
                    for (int i = 0; i < bundledCabIRs::count(); ++i)
                        if (text == bundledCabIRs::selectorName (i) || text == bundledCabIRs::displayName (i))
                            return (float) (i + 1);
                    return 0.0f;
                }));
        sourceParam = source.get();
        parameters->addChild (std::move (source));
    }

    startTimer (100); // sweeps irSlot and applies IR Source changes -- see DeferredReclaimer
}

IRLoaderProcessor::~IRLoaderProcessor() = default;

void IRLoaderProcessor::loadImpulseResponse (const juce::File& irFile)
{
    auto conv = std::make_unique<juce::dsp::Convolution>();

    if (sampleRate > 0.0)
    {
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) preparedBlockSize, (juce::uint32) preparedNumChannels };
        conv->prepare (spec);
    }

    // A guitar cabinet's impulse response is over within ~100 ms; what follows in a captured file is a noise floor
    // and room that no one hears, and convolving it is most of the cost (a 4 s file cost 18% of a core, the first
    // 150 ms about a tenth of that). A reverb IS its tail, so only the Cab role is capped.
    const int maxSamples = isReverbRole() || sampleRate <= 0.0 ? 0 : (int) (0.15 * sampleRate);
    conv->loadImpulseResponse (irFile,
                                juce::dsp::Convolution::Stereo::yes,
                                juce::dsp::Convolution::Trim::yes,
                                (size_t) maxSamples, // 0 -- use the whole file
                                juce::dsp::Convolution::Normalise::yes);

    lastLoadedFile = irFile;
    loadedName = irFile.getFileName();
    irSlot.publish (std::move (conv));

    // A file load is an explicit choice of the "File" source -- keep the
    // selector in step so the UI shows what's actually playing.
    if (sourceParam != nullptr)
        *sourceParam = 0.0f;
    appliedSource = 0;
}

void IRLoaderProcessor::loadBundledIR (int index)
{
    auto conv = std::make_unique<juce::dsp::Convolution>();

    const int maxSamples = isReverbRole() || sampleRate <= 0.0 ? 0 : (int) (0.15 * sampleRate);
    // BinaryData bytes are static storage -- Convolution's background decode
    // may keep reading them after this call returns, which is safe here
    // specifically because they can never move or be freed.
    conv->loadImpulseResponse (bundledCabIRs::data (index), (size_t) bundledCabIRs::dataSize (index),
                               juce::dsp::Convolution::Stereo::yes,
                               juce::dsp::Convolution::Trim::yes,
                               (size_t) maxSamples,
                               juce::dsp::Convolution::Normalise::yes);

    // Load BEFORE prepare: the docs guarantee prepare() fully initialises the
    // most recently supplied IR, so a bundled cab is active on the very first
    // process() call instead of fading in on the decode thread.
    if (sampleRate > 0.0)
    {
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) preparedBlockSize, (juce::uint32) preparedNumChannels };
        conv->prepare (spec);
    }

    loadedName = bundledCabIRs::selectorName (index);
    irSlot.publish (std::move (conv));
    // lastLoadedFile is deliberately kept -- flipping the selector back to
    // "File" restores the user's own IR rather than losing it.
}

void IRLoaderProcessor::applySourceSelection()
{
    const int wanted = sourceParam != nullptr
        ? juce::jlimit (0, bundledCabIRs::count(), (int) std::lround (sourceParam->get()))
        : 0;

    if (wanted == appliedSource)
        return;
    appliedSource = wanted;

    if (wanted > 0)
        loadBundledIR (wanted - 1);
    else if (lastLoadedFile.existsAsFile())
        loadImpulseResponse (lastLoadedFile); // rewrites appliedSource = 0, same value
    else
    {
        irSlot.publish (nullptr);
        loadedName.clear();
    }
}

void IRLoaderProcessor::clearImpulseResponse()
{
    irSlot.publish (nullptr);
    lastLoadedFile = juce::File();
    loadedName.clear();

    if (sourceParam != nullptr)
        *sourceParam = 0.0f;
    appliedSource = 0;
}

void IRLoaderProcessor::prepare (double newSampleRate, int maxBlockSize, int numChannels)
{
    sampleRate = newSampleRate;
    preparedBlockSize = maxBlockSize;
    preparedNumChannels = juce::jmax (1, numChannels);

    dryScratch.setSize (preparedNumChannels, maxBlockSize, false, false, true);

    // JUCE normalises a loaded IR to 0.125 / sqrt (sum of squares), i.e. an energy gain of -18 dB: a "Reverb" wet path is 18 dB
    // under the dry until this brings it back. A cab IR through guitar content measures closer to -8 dB (the box bump and
    // presence lift keep more of the passband), so it starts nearer its converged gain. Either way WetLevelMatcher then
    // follows the actual IR and signal.
    const float initialGain = isReverbRole() ? 1.0f / 0.125f : 2.5f;
    for (auto& m : wetMatch)
        m.prepare (sampleRate, initialGain);

    // Same reasoning as NAMProcessor::prepare(): a sample-rate/block-size
    // change invalidates an already-prepared Convolution, so rebuilding
    // whichever source is selected and swapping it in atomically rather
    // than mutating the live one. appliedSource = -1 forces the rebuild.
    appliedSource = -1;
    applySourceSelection();
}

void IRLoaderProcessor::reset()
{
    // Convolution's internal tail state isn't safe to reach into from here
    // without risking a race with the audio thread -- see prepare()'s
    // reload-on-change comment for how state actually resets.
}

void IRLoaderProcessor::process (juce::AudioBuffer<float>& buffer)
{
    auto* conv = irSlot.currentRaw();

    if (conv == nullptr)
        return; // no IR loaded -- pass through unchanged

    const int numSamples = buffer.getNumSamples();
    const float mix = mixParam->get();

    // Reaching here means an IR IS loaded, and the Mix law then applies in
    // full: the wet path is level-matched to the dry so Mix is a real
    // crossfade (docs/circuits/MixLaw.md). The old "a cab keeps its own
    // level" carve-out only held while a fresh Cab had no IR and passed
    // the input through; with a bundled IR active by default the cab would
    // sit ~8 dB under the dry and noon would no longer be 50/50.
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        dryScratch.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    conv->process (context);

    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        const auto* dry = dryScratch.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            wetMatch[(size_t) ch].accumulate (dry[i], data[i]);
            data[i] = dry[i] * (1.0f - mix) + data[i] * mix * wetMatch[(size_t) ch].gain();
        }
    }
    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
        wetMatch[(size_t) ch].endBlock (numSamples, 1);

    const float outGain = juce::Decibels::decibelsToGain (outputGainDb->get());
    if (outGain != 1.0f)
        buffer.applyGain (outGain);
}

juce::Colour IRLoaderProcessor::getAccentColour() const
{
    return isReverbRole() ? juce::Colour (0xff2d7a9e) : juce::Colour (0xff7a5c2d);
}

std::unique_ptr<juce::XmlElement> IRLoaderProcessor::getState() const
{
    auto xml = EffectProcessor::getState(); // base: mix/output gain

    if (lastLoadedFile != juce::File())
        xml->setAttribute ("irPath", lastLoadedFile.getFullPathName());

    return xml;
}

void IRLoaderProcessor::setState (const juce::XmlElement& state)
{
    EffectProcessor::setState (state); // base: mix/output/source params

    const bool savedByBundledAwareVersion = state.hasAttribute ("ir_source");
    const auto path = state.getStringAttribute ("irPath");

    if (sourceParam != nullptr && savedByBundledAwareVersion && std::lround (sourceParam->get()) > 0)
    {
        // New-format preset that explicitly picked a bundled IR -- that
        // selection wins over any stale irPath attribute also on the preset.
        // But keep the saved path around: flipping the selector back to File
        // afterwards must still find the custom IR.
        if (path.isNotEmpty())
        {
            const juce::File file (path);
            if (file.existsAsFile())
                lastLoadedFile = file;
        }
        applySourceSelection();
        return;
    }

    if (! savedByBundledAwareVersion && sourceParam != nullptr)
        *sourceParam = 0.0f; // pre-bundled preset: "no source recorded" meant file-or-nothing, keep that meaning

    if (path.isNotEmpty())
    {
        const juce::File file (path);
        if (file.existsAsFile())
        {
            loadImpulseResponse (file);
            return;
        }
        // else: the file moved or was deleted since the preset was saved --
        // leave this block unloaded rather than fail the whole preset load.
    }

    applySourceSelection(); // file source with no (valid) file -> clears
}

void IRLoaderProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // Both roles share cab.svg -- embedded SVG, not a hand-transcribed
    // juce::Path call; see IconKit.h for why. The Reverb role used to
    // borrow hall.svg as a stand-in (see docs/icons/AGENT-icon-notes.md),
    // but that glyph now belongs to HallReverbProcessor's real algorithmic
    // "Hall" block, so this generic loaded-IR box is the reuse until the
    // reference sheet's own grey "IR Loader" utility glyph gets confirmed.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::cab_svg, IconData::cab_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
