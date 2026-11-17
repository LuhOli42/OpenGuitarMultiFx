# Algorithmic pitch / synth effects

Processor: `Source/Effects/Pitch/AlgorithmicPitchProcessor.{h,cpp}`.

Poly Octave and Organ Machine voices: `gs::HybridShifter` (`Source/Effects/GuitarSynth/HybridShifter.h`, the
Guitar-to-Bass engines, see `docs/GuitarSynth.md`) run once on the mono sum, picked by **Mode**:
- MONO: `SpliceShifter` (correlation-chosen splices; going up the tap catches the input and jumps J back), ~5-15 ms,
  exact on single notes.
- POLY: `PhaseVocoderShifter`, every note of a chord in tune, ~32 ms.
- HYBRID (default): each attack from MONO, then a phase-aligned crossfade into POLY.

They replaced a fixed 40 ms two-tap granular shifter, which warbled at the window rate and pulled the +1 oct of a 220 Hz
note to ~420 Hz. Measured on sines (220 Hz / 1 kHz, every voice and mode): pitch exact, inharmonic energy ≤ −38 dB
(MONO ≤ −78 dB). The Whammy still uses the granular shifter (40 ms window, 80 ms down).

| Key | What it is |
|---|---|
| `PolyOctave` | EHX POG-style: Dry, −2, −1, +1, +2 octaves, each with its level. |
| `Whammy` | A bend from unison to the selected interval (+2 / +1 oct, +5th, +4th, −2nd, −1 / −2 oct, detune ±) by **Position** — map it to an expression pedal with MIDI Learn. Mix. |
| `OrganMachine` | EHX B9-style: 16' (sub), 8' (dry), 4' (octave) and 2 2/3' (19th) "drawbars", a key click on attacks, Mod = a rotating-speaker shimmer. |
| `MonoSynth` | Tracks the played pitch like analog guitar synths did (low-pass, level-tracking comparator, period measurement), drives a saw / square oscillator (Octave −1 / 0 / +1, Glide) through a resonant VCF with an envelope (Cutoff, Resonance, Env Amount), Mix. |
