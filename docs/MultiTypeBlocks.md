# Multi-Type blocks

One generic engine per category (compressor, chorus, flanger, phaser, delay, reverb); every "type" is a preset of
that engine's parameters. Each type is also registered as its own block (`MtCmp_*`, `MtChr_*`, `MtFln_*`,
`MtPhs_*`, `MtDly_*`, `MtRvb_*`) under the "Multi-Type" chip of its category.

All DSP is written here; type parameter values come from public pedal and amp schematics. Sources:
`Source/Effects/MultiType/`, registration in `EffectRegistry.cpp`.

## Panel: front page + More

Every Multi-Type block follows one rule (`mt::PanelLayout`, `EffectProcessor::getPanelLayout()`):

- **Front**: Type (generic block only), the real pedal's knobs, plus the knob the type is named for, then Mix, Level.
- **More**: fine tuning: cut filters, ratio/release, stereo and routing, the type's second-order controls.
- Everything else is the type's voicing: hidden, still set by the type, saved in presets and snapshots, MIDI-learnable.

| Block | Front | + by type | More |
|---|---|---|---|
| Delay | Time, Feedback, Wet, Level | Mod, Drive, Bits, Ducker, Sweep Rate | High/Low Cut, Mod Rate, L/R Ratio, Fb R/Cross, Head 2, Diffusion, Ducker Threshold, Resonance, Kill Dry |
| Reverb | Time, High Cut, Wet, Level | Pitch Mix (shimmer); spring: Dwell, Time, Spring Tone | Pre-Delay, Size, Mod Depth (spring: Boing, High Cut), Low Cut, Kill Dry |
| Chorus | Rate, Depth, Mix, Level | | High Cut |
| Flanger | Rate, Depth, Feedback, Manual, Level | | Mix, LFO Quantize (step type) |
| Phaser | Rate, Depth, Feedback, Level | | Manual, Tone, Mix |
| Compressor | Threshold, Attack, Mix, Level | Dynamics | Ratio, Release |
| Pitch | Whammy: Position, Range; Harmony: Key, Scale, Harmony 1-2; Shift/Detune 1-2 | | voices 3-4, voice levels, delays, LFO, Low/High Cut (generic: Mode, Voices) |

`Tests/MultiTypePanelTests.cpp` checks the shape (two pages, front <= 8 ending in Level, Type first on the generic block).

## Pitch

`MultiTypePitchProcessor` (types registered as `MtPit_*`, in the Pitch category): up to 4 voices over
one shared delay line. Each voice is a two-grain time-domain shifter: two read pointers half a grain apart slide at
`(1 - ratio)` samples per sample and are cross-faded with complementary smooth windows. Modes: FIXED (per-voice
shift + detune + delay + level + pan), WHAMMY (pedal position over UP/DOWN 1-2 octaves or UP|DN) and DIATONIC (a
small time-domain pitch detector finds the played note, which is placed in Key/Scale; each voice moves it by its
Harmony steps in scale degrees). An LFO modulates the detune with opposite sign on alternating voices. No FFT.
In Diatonic mode the status line under the block shows the detected note.
