# Analog octave-down pedals (Octave Multiplexer, Octoplus, OCT-1)

Processor: `Source/Effects/Pitch/DividerOctaverProcessor.{h,cpp}`, shared front end `PitchParts.h::Analyser` (low-pass,
peak detectors, a comparator whose thresholds follow the note's level — a decaying string keeps tracking) and
`Dividers` (4013 toggle flip-flops). Block-level, like the project's Boss OC-2.

| Pedal | Key | How the octave is made | Controls |
|---|---|---|---|
| EHX Deluxe Octave Multiplexer (EH-5950, 1978) | `OctaveMultiplexerStyleOctaver` | 4013 → **2N4302 choppers** switching the analyser-filtered signal between its two phases (the note's own waveform an octave down) | Blend (dry ↔ sub), Filter (Tone), Bass switch |
| DOD FX35 Octoplus | `FX35StyleOctaver` | 3rd-order analyser LPF, 4013 + **J113 chopper**, octave out through 220K / .005 / 220K / 120p | Direct Level, Octave Level, Tone |
| Korg OCT-1 | `OCT1StyleOctaver` | 4013s, the **divided square** through an NJM13600 VCA that follows the note's envelope — synth-like | Direct Level, Effect Level, Octave −1 / −2 |

Monophonic like the originals: chords and noisy decays mis-track the same way.
