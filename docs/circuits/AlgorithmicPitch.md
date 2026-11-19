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
| `MonoSynth` | Tracks the played pitch with `pitchparts::NoteTracker` (see below), drives a saw / square oscillator (Octave −1 / 0 / +1, Glide) through a resonant VCF with an envelope (Cutoff, Resonance, Env Amount), Mix. |

## Mono Synth note tracking (`Source/Effects/Pitch/NoteTracker.h`)

The first version took every period a single comparator measured. As a note decays the fundamental beats against
the 2nd harmonic and the noise floor, so the comparator flipped octaves and wandered (on the guitar DI fixture:
about 3000 jumps over 60 cents per 10 ms frame in 38 s). NoteTracker keeps the analog front end and adds a lock:

- DC block, then a 4-pole low-pass that follows the locked note (1.4x its fundamental; 700 Hz before a lock).
- Rising zero crossings with peak-scaled hysteresis, timed to the sub-sample. The period is the smallest crossing
  count whose span repeats within 3 %, so a strong 2nd harmonic (two crossings per period) still reads as the
  fundamental.
- Lock: estimates within ~1 semitone refine the note (vibrato, bends). A jump must repeat 3 times (6 while the note
  decays) to be taken (legato notes); while decaying (below ~-9 dB of the note's peak) an octave / fifth / 3x jump is
  never taken, and vibrato is followed only within +-40 cents of the full-level pitch. A new attack unlocks at once.
- Below ~-36 dB under the note's peak (or about -60 dBFS) the pitch is held, not re-measured.

Result on the same fixture: about 140 jumps (mostly real note changes); decaying synthetic notes E2..E5 with a strong
2nd harmonic, beating partials, hum and noise stay within 30 cents to the end; note changes lock within ~50 ms.

