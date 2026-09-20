# Unity level: every pedal at noon = bypass loudness

## The problem
Measured with the same guitar-like signal (E3 with harmonics 1..10 at 1/k amplitude,
0.1 RMS = -20 dBFS) and every knob at noon, the pedals were nowhere near each other:

| Pedal | RMS gain vs bypass |
|---|---|
| Klon Centaur | +11.8 dB |
| BD-2 | +11.5 dB |
| Rangemaster-style booster | +3.2 dB |
| DS-1 | -2.0 dB |
| HM-2 | -6.3 dB |
| TS808 / TS9 / TS10 | -6.7 / -6.5 / -7.2 dB |
| OD-1 | -8.2 dB |

Real pedals differ like this too (each has its own Level range), but here they feed an
amp model: a pedal that is 20 dB hotter than another drives the amp 20 dB harder, and
"the pedals all sound alike" is partly that the loud ones flatten the amp the same way.

## The fix
`Source/Effects/OutputTrimEffect.h` multiplies a pedal's output by a fixed gain; the
registry (`EffectRegistry.cpp`) wraps each pedal in the trim that makes noon-everything
unity for that reference signal. The Level knob keeps its natural range around it.
`Tests/PedalUnityLevelTests.cpp` re-measures every registered pedal and fails outside
+-1 dB (all are within 0.06 dB today); it prints the new figure to copy into the trim
when a circuit changes.

Unity is defined for ONE input level: a hard clipper's RMS gain falls as the input
rises (HM-2: +0.2 dB at 0.05 RMS, -12 dB at 0.2 RMS), exactly like the real pedal.
