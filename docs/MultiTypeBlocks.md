# Multi-Type blocks

One generic engine per category (compressor, chorus, flanger, phaser, delay, reverb); every "type" is a preset of
that engine's parameters. Each type is also registered as its own block (`MtCmp_*`, `MtChr_*`, `MtFln_*`,
`MtPhs_*`, `MtDly_*`, `MtRvb_*`) under the "Multi-Type" chip of its category.

All DSP is written here; type parameter values come from public pedal and amp schematics. Sources:
`Source/Effects/MultiType/`, registration in `EffectRegistry.cpp`.

## Panel: pedal-sized controls

Every engine parameter exists and runs, but each block's `isParameterActive()` shows only the knobs a pedal of that
kind has plus the few that shape the current type (e.g. LFO rate/depth on modulated delays, Drive where the type
drives, Spring Tone/Drive/Boing on springs, the ducker on Ducking Delay). The rest is the type's voicing: set by
`applyType()`, still saved in presets and snapshots, just not on screen. Fixed-type blocks (`Mt*_N`) hide the Type
selector; the generic `MultiType*` blocks keep it and their extras follow the selected type.
`Tests/MultiTypePanelTests.cpp` caps every block at 14 shown controls and logs each block's list.

## Pitch

`MultiTypePitchProcessor` (types registered as `MtPit_*`, in the Pitch category): up to 4 voices over
one shared delay line. Each voice is a two-grain time-domain shifter: two read pointers half a grain apart slide at
`(1 - ratio)` samples per sample and are cross-faded with complementary smooth windows. Modes: FIXED (per-voice
shift + detune + delay + level + pan), WHAMMY (pedal position over UP/DOWN 1-2 octaves or UP|DN) and DIATONIC (a
small time-domain pitch detector finds the played note, which is placed in Key/Scale; each voice moves it by its
Harmony steps in scale degrees). An LFO modulates the detune with opposite sign on alternating voices. No FFT.
In Diatonic mode the status line under the block shows the detected note.
