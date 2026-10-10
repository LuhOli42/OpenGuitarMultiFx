#pragma once

#include "../Effects/EffectProcessor.h"

#include <juce_core/juce_core.h>

#include <array>
#include <utility>
#include <vector>

namespace openguitarmultifx::snapshotxml
{

/**
    Snapshots -- a preset's internal variations A-H, Helix-style (see
    docs/PresetsAndSnapshots.md, which is the spec this implements).
    A snapshot stores every block's bypass state and every float
    parameter's value -- nothing else. It can never add, remove, reorder
    or swap blocks, change routing, or load a different NAM model/IR:
    that is what a preset is for. Because only parameters and bypass
    flags move, a snapshot switch never rebuilds the SignalGraph -- the
    same processors keep running and delay/reverb tails ring through
    (gapless, the whole point of the feature), with block enable/disable
    riding EffectProcessor::processWithBypass()'s crossfade + trails
    exactly like a hand-toggled bypass switch.
*/

constexpr int maxSnapshots = 8;

/** MIDI CC reserved for snapshot selection -- value 0-7 selects
    snapshots A-H, the same convention Line 6 uses (see the doc's MIDI
    row). It is deliberately NOT learnable onto a parameter:
    MainComponent::handleMidiCc() dispatches it before the
    MIDI-Learn/binding path. */
constexpr int snapshotSelectCcNumber = 69;

/** One block's captured state inside a snapshot: its bypass flag plus
    every AudioParameterFloat as (paramID, value), in the same order
    getParameters(true) enumerates them. */
struct BlockSnapshot
{
    bool bypassed = false;
    std::vector<std::pair<juce::String, float>> params;
};

struct Snapshot
{
    juce::String name;   // empty = unlabelled, shown as its letter
    juce::String colour; // hex string ("ffrrggbb"), empty = default
    // One entry per <Block> of the preset, in chain order -- blocks are
    // matched by POSITION, not by key, because a snapshot can never
    // change which blocks exist.
    std::vector<BlockSnapshot> blocks;
};

/** Live state of `processor` captured as a BlockSnapshot. Control thread only. */
BlockSnapshot captureBlock (EffectProcessor& processor);

/** Pushes a captured BlockSnapshot back into `processor`: parameters
    first (each AudioParameterFloat written through its own operator=,
    so the processor's existing parameter smoothing applies), then the
    bypass flag -- read by processWithBypass() on the audio thread, so
    enable/disable changes crossfade like a manual toggle. Params whose
    paramID isn't in the snapshot keep their current value; snapshot
    entries the processor doesn't have are skipped. Control thread only. */
void applyBlock (EffectProcessor& processor, const BlockSnapshot& snapshot);

/** Appends the <Snapshots> element to a preset being saved. Always
    writes all eight slots -- unused ones just hold defaults, matching
    the "8 fixed slots" model the doc chose. `currentIndex` records
    which snapshot was live at save time so a reload lands back on it. */
void write (juce::XmlElement& presetXml,
            const std::array<Snapshot, maxSnapshots>& snapshots,
            int currentIndex);

/** Reads the <Snapshots> element of a preset being loaded into
    `snapshots` (fully replaced) and the saved live index into
    `currentIndex` (clamped to 0-7). Returns false when the preset has
    no <Snapshots> element at all -- every preset saved before this
    feature -- so the caller can fall back to a default single-state
    instead of misreading old files. Snapshot indices outside 0-7 are
    skipped, not fatal. */
bool read (const juce::XmlElement& presetXml,
           std::array<Snapshot, maxSnapshots>& snapshots,
           int& currentIndex);

/** "A".."H" for 0..7. */
juce::String letterFor (int index);

/** Fallback colour per slot when the snapshot doesn't name one --
    eight easily-distinguished hues, same role as the per-effect accent
    colours elsewhere in the UI. */
juce::Colour defaultColourFor (int index);

/** CC 69 value -> snapshot index: 0-7 map to A-H, anything else is
    ignored (-1), same as a Helix receiving an out-of-range value. */
int indexForCcValue (int ccValue);

} // namespace openguitarmultifx::snapshotxml
