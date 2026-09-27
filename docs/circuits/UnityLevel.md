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
| Distortion+ / DOD 250 / Guv'nor / Blues Breaker / RAT (added later) | -6.2 / -0.5 / -0.06 / -5.3 / +3.05 dB |

**Re-measured 2026-09-21** after the pot taper was corrected from `knob^2` to a real 15% audio taper
([GainStaging.md](GainStaging.md)): every log-pot pedal lost level at noon and its trim moved by that much --
TS808/9/10 +4.6/+4.6/+4.4, BD-2 +6.1, Blues Breaker +4.7, Guv'nor +4.4, RAT +4.2, DOD 250 +3.5, Distortion+ +2.8,
OD-1 +0.4, Centaur -0.3, Bassman +0.1. The pedals whose pots are all linear (DS-1, HM-2, Booster) did not move.
Current values live in `Source/EffectRegistry.cpp`; `PedalUnityLevelTests` prints and guards them.
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

The **Bassman-Style Amplifier** is in the same test with a trim of -7.70 dB (was -14.15 before the speaker became a real impedance): at noon (all seven page-1 knobs at 0.5) it is
+8.4 dB hot for the reference signal (two channels at half volume into ~50 W). As for the pedals the number is for
that one knob position; a real amp's loudness follows its Volume knobs.

**2026-09-26, new pedals** (noon, before the trim): RAT 2 -1.25 dB, Turbo RAT +7.85, Crunch Box +14.25, Zendrive +10.14, OCD +14.37; the registry
trims are the negatives (+1.25 / -7.85 / -14.25 / -10.14 / -14.37). The OCD's two selectors are at their defaults (MOSFET clipping, High peak).
