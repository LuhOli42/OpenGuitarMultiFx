# Algorithmic pitch / synth effects

Processor: `Source/Effects/Pitch/AlgorithmicPitchProcessor.{h,cpp}`. Shifting voices: a delay line read by two taps
sweeping a 40 ms window (80 ms for downward voices) at (1 − ratio) × real time, sin²-crossfaded — polyphonic, ~one window
of latency, the usual granular smear on chords.

| Key | What it is |
|---|---|
| `PolyOctave` | EHX POG-style: Dry, −2, −1, +1, +2 octaves, each with its level. |
| `Whammy` | A bend from unison to the selected interval (+2 / +1 oct, +5th, +4th, −2nd, −1 / −2 oct, detune ±) by **Position** — map it to an expression pedal with MIDI Learn. Mix. |
| `OrganMachine` | EHX B9-style: 16' (sub), 8' (dry), 4' (octave) and 2 2/3' (19th) "drawbars", a key click on attacks, Mod = a rotating-speaker shimmer. |
| `MonoSynth` | Tracks the played pitch like analog guitar synths did (low-pass, level-tracking comparator, period measurement), drives a saw / square oscillator (Octave −1 / 0 / +1, Glide) through a resonant VCF with an envelope (Cutoff, Resonance, Env Amount), Mix. |
