# Multi-Type blocks

One generic engine per category (compressor, chorus, flanger, phaser, delay, reverb); every "type" is a preset of
that engine's parameters. Each type is also registered as its own block (`MtCmp_*`, `MtChr_*`, `MtFln_*`,
`MtPhs_*`, `MtDly_*`, `MtRvb_*`) under the "Multi-Type" chip of its category.

All DSP is written here; type parameter values come from public pedal and amp schematics. Sources:
`Source/Effects/MultiType/`, registration in `EffectRegistry.cpp`.

## Pitch

`MultiTypePitchProcessor` (types registered as `MtPit_*`, in the Pitch category): up to 4 voices over
one shared delay line. Each voice is a two-grain time-domain shifter: two read pointers half a grain apart slide at
`(1 - ratio)` samples per sample and are cross-faded with complementary smooth windows. Modes: FIXED (per-voice
shift + detune + delay + level + pan), WHAMMY (pedal position over UP/DOWN 1-2 octaves or UP|DN) and DIATONIC (a
small time-domain pitch detector finds the played note, which is placed in Key/Scale; each voice moves it by its
Harmony steps in scale degrees). An LFO modulates the detune with opposite sign on alternating voices. No FFT.
In Diatonic mode the status line under the block shows the detected note.
