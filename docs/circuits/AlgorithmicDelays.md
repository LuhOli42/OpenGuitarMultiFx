# Algorithmic delays

Processor: `Source/Effects/Delay/AlgorithmicDelayProcessor.{h,cpp}`. Level adds the repeats to the full dry signal
(an E.Level, not a crossfade). Time 20 ms – 1.5 s (exponential).

| Key | What it is |
|---|---|
| `DuckingDelay` | After the TC 2290's dynamic delay: an envelope follower (5 ms attack, Release 50 ms–1.5 s) pulls the repeats down while you play (Duck = how far) and lets them swell back in the gaps. |
| `ShimmerDelay` | A delay-line pitch shifter (two taps sweeping a 60 ms window, sin² crossfaded) inside the feedback loop: each repeat an octave (or a fifth) higher than the last, with a Tone low-pass in the loop. |
| `LoFiDelay` | Each pass is sample-held at a lower rate, quantised to fewer bits (12 → 4) and darkened (7 → 1.5 kHz), with a slow wobble: a memory that degrades as it repeats. |
| `GranularDelay` | Hann-windowed grains (20–300 ms, Grain) spawned 2–40 times a second (Density) from random points of the delay buffer; Reverse and Octave set the odds a grain plays backwards / an octave up. |
