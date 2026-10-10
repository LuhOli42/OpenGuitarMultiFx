# Presets

## Purpose
Owns: plain file I/O for presets (`PresetManager`) — one XML file per preset under a presets directory, plus stable preset-number bookkeeping.
Does not own: what a preset's XML actually contains — `MainComponent` builds/interprets the `XmlElement` (`buildPresetXml()`/`applyPresetXml()`); `PresetManager` deliberately knows nothing about `EffectProcessor`, `EffectRegistry`, or the signal chain itself, same separation `Tone3000Manager` keeps from its UI.

## Code Map
| Looking for... | Go to |
|----------------|-------|
| Save/load/list/delete presets | `PresetManager.{h,cpp}` |
| What actually goes into a preset's XML (chain, params, model refs) | `Source/UI/MainComponent::buildPresetXml`/`applyPresetXml` (not this directory) |

## Public API
| Export | Used By | Change Impact |
|--------|---------|---------------|
| `PresetManager::numberForExistingPreset`/`nextAvailableNumber` | `MainComponent::savePresetAs` | A preset's number is assigned once and kept on every re-save — treat it as stable identity, not list position |
| `PresetManager::renamePreset`/`duplicatePreset`/`presetExists` | `MainComponent::showPresetsPanel` | Rename keeps the number (same preset, new name); duplicate takes the next free number. Both refuse an empty or already-taken name instead of overwriting |

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

## Pitfalls
- Before commit `921e0e9`, only a processor's float parameters round-tripped through presets — a preset couldn't actually restore which NAM model or IR file was loaded, only gain/mix knobs. Fixed by `NAMProcessor`/`IRLoaderProcessor` overriding `getState()`/`setState()` (see `Source/Effects/AGENTS.md`), not by anything in this directory — if a new processor type gains file-backed state, its own `getState()`/`setState()` override is where that belongs, not `PresetManager`.

## Patterns

### Adding a field to what gets saved in a preset
1. This directory only writes/reads whatever `XmlElement` it's handed — it never needs a change for a new field.
2. The actual capture/restore logic lives in `Source/UI/MainComponent::buildPresetXml`/`applyPresetXml`, or in a processor's own `getState()`/`setState()` override if the field is per-processor state (see `Source/Effects/AGENTS.md`).

## Boundaries

### Never
- Have `PresetManager` reach into `EffectProcessor`, `EffectRegistry`, or the signal chain directly — it stays a plain XML file-I/O layer; `MainComponent` is the only place that interprets preset content.
