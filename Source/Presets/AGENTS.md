# Presets

## Purpose
Owns: plain file I/O for presets (`PresetManager`) — one XML file per preset under a presets directory, plus stable preset-number bookkeeping.
Does not own: what a preset's XML actually contains — `MainComponent` builds/interprets the `XmlElement` (`buildPresetXml()`/`applyPresetXml()`); `PresetManager` deliberately knows nothing about `EffectProcessor`, `EffectRegistry`, or the signal chain itself, same separation `Tone3000Manager` keeps from its UI.

## Code Map
| Looking for... | Go to |
|----------------|-------|
| Save/load/list/delete presets | `PresetManager.{h,cpp}` |
| Tags / bank slots / setlists / search (the browser's organisational layer) | `PresetLibrary.{h,cpp}` -- wraps `PresetManager`, reads every file fresh; the browser's only entry point |
| Pure XML (de)serialization for that metadata | `PresetMetadataXml.h` (`presetmeta::` namespace -- same pattern as `MidiLearnPresetXml.h`) |
| What actually goes into a preset's XML (chain, params, model refs) | `Source/UI/MainComponent::buildPresetXml`/`applyPresetXml` (not this directory) |

## Public API
| Export | Used By | Change Impact |
|--------|---------|---------------|
| `PresetManager::numberForExistingPreset`/`nextAvailableNumber` | `MainComponent::savePresetAs` | A preset's number is assigned once and kept on every re-save — treat it as stable identity, not list position |
| `PresetManager::renamePreset`/`duplicatePreset`/`presetExists` | `MainComponent::showPresetsPanel` | Rename keeps the number (same preset, new name); duplicate takes the next free number. Both refuse an empty or already-taken name instead of overwriting |
| `PresetLibrary` (`entries`/`search`/`assignToSlot`/setlist ops) | `PresetBrowserDialog` | Everything the browser knows about a preset is `PresetLibrary::Entry` (name, number, tags, bank slot, setlist seats); mutation goes through `editPresetXml()` -- a load/edit/save round-trip per write |

## Entry Points
| Task | Start Here |
|------|------------|
| Change preset file format/storage | `PresetManager.{h,cpp}` |
| Change what a preset actually captures | `Source/UI/MainComponent::buildPresetXml`/`applyPresetXml` (not this directory) |

## Roadmap
The plan and the decisions for the preset system (snapshots inside a preset, browser, setlists) are in
`docs/PresetsAndSnapshots.md` -- read it before changing what a preset contains.

## Contracts
- A preset's `number` attribute is a stable identity assigned once (commit `0050b30`) — like a numbered slot on a hardware pedalboard, never recomputed from position on re-save.
- Model/IR references inside a preset are by checksum, not absolute path (per ARCHITECTURE.md section E) — this directory doesn't enforce that itself, but callers building the XML must not embed absolute paths.
- `listPresetNames()` returns empty (not an error) when the presets directory doesn't exist yet — "no presets saved" is a normal starting state, not a failure to handle.
- Organisational metadata is ADDITIVE and optional: `<Tags>`/`<Setlists>` child elements and `bank`/`slot` attributes on `<Preset>`; a file without any of them (every preset saved before this) reads as untagged + unassigned. Never restructure `PresetManager`'s core schema to express these.
- A setlist has no definition file -- it exists iff at least one preset's `<Setlists><Member name index/></Setlists>` references it; order is (index, preset number); an emptied setlist just stops existing. Adding/removing/moving members is a per-file write on the affected preset(s).
- `renamePreset`/`duplicatePreset` copy the whole XML, so tags/memberships travel automatically. `MainComponent` strips memberships (not tags) on duplicate via `PresetLibrary::clearMemberships()` -- a copy must not steal the original's footswitch slot or setlist seat.
- `PresetLibrary` rescans the directory on every read (no cache) -- safe because preset counts are small and this runs on the control thread only.

## Pitfalls
- Before commit `921e0e9`, only a processor's float parameters round-tripped through presets — a preset couldn't actually restore which NAM model or IR file was loaded, only gain/mix knobs. Fixed by `NAMProcessor`/`IRLoaderProcessor` overriding `getState()`/`setState()` (see `Source/Effects/AGENTS.md`), not by anything in this directory — if a new processor type gains file-backed state, its own `getState()`/`setState()` override is where that belongs, not `PresetManager`.

## Patterns

### Adding a field to what gets saved in a preset
1. This directory only writes/reads whatever `XmlElement` it's handed — it never needs a change for a new field.
2. The actual capture/restore logic lives in `Source/UI/MainComponent::buildPresetXml`/`applyPresetXml`, or in a processor's own `getState()`/`setState()` override if the field is per-processor state (see `Source/Effects/AGENTS.md`).

### Adding organisational metadata (tags/setlists/...)
1. Extend `PresetMetadataXml.h` with a pure read/write helper pair for a new optional element/attribute.
2. Surface it on `PresetLibrary::Entry` and add `PresetLibrary` mutators; the browser never touches files directly.
3. Add it to `presetmeta::copyOrganisation()` or a resave (`savePresetAs`) drops it.

## Boundaries

### Never
- Have `PresetManager` reach into `EffectProcessor`, `EffectRegistry`, or the signal chain directly — it stays a plain XML file-I/O layer; `MainComponent` is the only place that interprets preset content.
