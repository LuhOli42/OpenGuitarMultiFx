# Presets and snapshots

Plan for the preset system and the snapshots inside a preset. Decided with the user on 2026-10-10 after
looking at how commercial units do it. Each stage below has its own card on the Notion board.

## Decisions

| Question | Decision |
|---|---|
| Variations inside a preset | **Snapshots**, Helix-style. Not Fractal-style channels per block. |
| How many per preset | **8** (A–H), each with a name and a colour. |
| What a snapshot stores | Every block's bypass state and **every** float parameter of every block, automatically. The user does not mark which knobs are snapshot-controlled. A knob whose value differs between snapshots gets a small marker. |
| What a snapshot cannot do | Add, remove, reorder or swap blocks, change routing, or load a different NAM model / IR. That is what a preset is for. |
| Edits while playing | **Recall**: a knob edit stays in the current snapshot and is still there after switching away and back, until the preset is saved or the changes are discarded. |
| MIDI | Program Change keeps selecting the preset. **CC 69** selects the snapshot (value 0–7 = A–H), the same convention as Line 6. |
| Display | The preset badge shows number + letter, e.g. **"3B"**. |

## Why snapshots are gapless and presets are not

Loading a preset destroys and recreates every processor (`MainComponent::applyPresetXml()`), so delay and reverb
tails are lost and the new processors start from silence. A snapshot keeps the same processors running and only
writes parameter values and bypass flags, so the graph is never rebuilt.

That only sounds clean if the engine handles jumps well. The code has three gaps that stage 2 fixes:

1. ~~Bypass is a hard cut~~: fixed in stage 2a -- a 10 ms crossfade in `EffectProcessor::processWithBypass()`.
2. About 27 processors do not smooth parameter changes. Turning a knob by hand hides it; a snapshot jumps the
   value in one block and clicks.
3. ~~Delays and reverbs have no trails~~: fixed in stage 2a -- `hasTrails()`, always on for the 14 delays/reverbs.

## How other units do it (research, 2026-10-10)

| Unit | Inside a preset | Organisation |
|---|---|---|
| Line 6 Helix | Up to 8 snapshots: bypass + parameter values, no block swaps, trails continue | Setlists, banks of 4 or 8 |
| Neural DSP Quad Cortex | Scenes; `*` marks an edited preset | Setlists of 32 banks x 8; preset switch cuts audio, scene switch does not |
| Fractal Axe-Fx | Scenes plus 4 channels per block | Numbered list |
| Kemper | Performances with 5 slots and morphing | Performances |
| Boss GT-1000 | Delay carry-over between patches (same delay type only) | Banks of 5, live sets |

## Stages

1. **Preset fixes**: confirm before delete, `*` marker for unsaved changes, save/discard prompt before switching
   preset, rename, duplicate.
2. **Engine**: bypass crossfade, parameter smoothing in every processor that lacks it, trails for delays and
   reverbs.
3. **Snapshots**: XML format, A–H bar, CC 69, "3B" badge, Recall editing.
4. **Preset browser**: card grid with the chain's icons, banks of 4/8, setlists, search, tags, favourites.
5. **Preset spillover**: delay/reverb tails surviving a preset change. Hardest; last.
