# "Everything sounds hard and bright": what was measured (2026-09-26)

## The report
After a listening session with the pedals built so far: (1) the OCD's MOSFET / LED switch does not seem to change anything; (2) all the overdrives sound alike; (3)
the highs of the whole app are harsh and hard, "artificial" next to the real thing, and it might come from how the saturation is calculated.

## Method
`Tests/PedalDiagnostics.cpp`, run by name with `PEDAL_DIAG=1` (dev only): a 293 Hz or 1.1 kHz sine (0.1 V peak) through each drive pedal at its registry default,
3 s, FFT of the last 0.68 s; "alias" = energy that is not on a harmonic of the note, between 60 Hz and 16 kHz (sub-audio drift and the decimator's near-Nyquist leakage
are excluded on purpose: the first version of the metric counted a 23.8 kHz leftover of the half-band filter as "aliasing" and was wrong), relative to the harmonic energy.

## 1. The OCD switch works; drive hides it
| Drive / input | MOSFET: level, H3, energy above 4 kHz | LED: level, H3, above 4 kHz |
|---|---|---|
| 0.3 / 0.03 V | -12.0 dB, -89, -137 | -12.0 dB, -127, -139 (both clean) |
| 0.3 / 0.10 V | -4.2 dB, -20.5, **-65.0** | -3.7 dB, -20.1, **-49.1** (16 dB more highs) |
| 0.6 / 0.10 V | -3.0 dB, -14.7, -44.8 | -2.9 dB, -13.9, -40.0 |
| 0.9 / 0.30 V | -2.8 dB, -13.6, -25.5 | -2.7 dB, -13.3, -22.3 |
With the clipper node limited to +-0.67 V (MOSFET) the second stage (x4.85) outputs 3.2 V against rails at +-3.0 V: almost at the limit. With LEDs (+-1.7 V) it is 8 V: the op-amp
clips hard. From Drive 0.6 up both are a rail-limited square wave, and the clipper's difference is gone. That is what the schematic does too (Aion: the LED mode "has nothing left to
clip" in stock mode); it is audible at low drive and with the guitar turned down.

## 2. Why the overdrives sound alike at defaults
THD (H2..H12) of a 293 Hz sine against the input level, every pedal at its default (`sensitivity` test):
the TS808 / TS9 / TS10 give the same numbers (same circuit); OD-1, DOD 250, ODR-1, OCD, Zendrive and the Guv'nor are at 7-13% at 40 mV and 16-29% at 160 mV; DS-1, HM-2, RAT, DT-1
and the Metal Zone are at 8-19% at 5 mV and 30-40% from 80 mV (saturated for any guitar). Once every pedal is squashed into a near-square wave, their harmonic profile converges
(H3 ~ -14 dB, H5 ~ -23 dB, H7 ~ -30 dB). The differences the circuits do have (mid hump, treble boost, asymmetry) are in the linear response and at low levels: they are verified
against the closed forms in each pedal's tests, but they disappear when everything is driven into the rails. The remedy is level (`GainStaging.md`: the app reads 1.0 as one volt; a guitar
is 0.05-0.5 V), not another model.

## 3. Highs: aliasing, at the default quality
Non-harmonic energy in 60 Hz .. 16 kHz for a 1.1 kHz sine at 0.1 V (dB against the harmonics; the closer to 0, the worse), Eco (the default) / Normal / High:

| Pedal | Eco | Normal | High |
|---|---|---|---|
| DS-1 | **-22.6** | -51.6 | -59.2 |
| Metal Zone | **-26.2** | -34.2 | -51.6 |
| HM-2 | -32.7 | -45.4 | -62.0 |
| RAT / RAT 2 | -32.9 / -33.0 | -47.4 / -49.2 | -62.2 / -62.0 |
| BD-2 | -42.3 | -57.4 | -69.4 |
| Crunch Box | -42.3 | -54.1 | -66.2 |
| OCD | -45.7 | -64.8 | -83.8 |
| Guv'nor | -52.1 | -52.1 | -67.5 |
| Turbo RAT | -50.6 | -61.6 | -77.7 |
| DT-1 | -47.9 | -71.0 | -102.9 |
| TS808 / TS9 / TS10 | -67.8 / -67.8 / -68.5 | same | -99.8 / -99.8 / -101 |
| OD-1, DOD 250, Zendrive, ODR-1 | -62 .. -80 | | |
| Klon, Blues Breaker, Overdriver, Distortion+ | < -100 | | |
Every soft-clipping pedal (diodes in a feedback loop, small gain) is clean at 1x; the hard clippers are not, and -22 .. -33 dB is loud, inharmonic, and exactly what "digital, hard, bright" sounds
like. A 293 Hz note (lower harmonics spacing) is 3-5 dB better than these numbers, so the guitar's upper notes and chords are the worst case.

**Changed:** the Eco tier of the DS-1, HM-2, RAT and RAT 2 is now 2x (Normal 4x; High 8x, HM-2 4x): DS-1 4.6% instead of 2.6% of a core, RAT ~3.8% instead of 1.9%, HM-2 ~12% instead of 6%.
`OversampledEffectTests` states the new tiers. **Not changed:** the Metal Zone (1x = 15% of a core: 2x would be ~30%), the BD-2 / OCD / Crunch Box / Guv'nor (-42 .. -52 dB at Eco: borderline).

## What is NOT a cause (checked)
* The half-band filter, the solver's theta (0.6) and the block cuts: the near-Nyquist leftover (23.8 kHz, -39 dB in the DS-1 at 4x) is the 22nd harmonic (24.17 kHz) leaking through the
  decimator's transition band and is inaudible; below 16 kHz the oversampled outputs are clean.
* An unmodelled slew rate rounds edges over 12-33 us: about one sample at 48 kHz, so it changes nothing at 1x; it matters only in the oversampled modes.

## What this does not settle
There is no reference recording of a real pedal in this project, so "brighter than the real one" cannot be measured; only the aliasing above is a demonstrated defect. Still open, in order of
suspicion: the level the interface hands the app (Input gain -12 dB), the missing speaker / cab in the comparison (a distortion pedal straight into a DI is fizzy in real life), and the
untested hard-rail clipping of the op-amp macro (its knee is perfectly sharp; a real output stage rounds over ~0.5 V). To measure the last two: 10 s of DI guitar and 10 s of the same
playing through the real pedal at known knob positions.

## Against neural captures of real pedals (2026-09-26)

The user's own suggestion: compare the models with NAM captures of real pedals. Bench: `Tests/NamPedalCompare.cpp` (`NAM_COMPARE=<dir of .nam>`,
optionally `NAM_COMPARE_CAB=<IR wav>` to append the Cab block, `NAM_COMPARE_AMPS=1` for the amps). The captures used (research only, not shipped): a TS808 capture
(deathblossomaudio, TONE3000, via the dot-pedalboards repo) and the PRE-pedal captures of guitarlum/VoLum (a Klon clone, a Bender, Petty John drives...). Knob positions of the captures are
unknown, so the shape is compared, not the loudness. Phrase = synthetic plucked chord + high notes; bands are dB re 100-1000 Hz.

| unit (input -20 dBFS RMS) | 1-2k | 2-4k | 4-8k | 8-16k |
|---|---|---|---|---|
| NAM TS808 | -4.5 | -12.1 | -21.5 | -31.9 |
| model TS808 | -5.0 | -12.9 | -22.5 | -33.9 |
| NAM Petty John Nuke / Myth (hard drives) | -4.8 / -3.7 | -10.7 / -9.6 | -17.6 | -26.5 / -26.9 |
| model RAT / OCD | -4.9 / -5.0 | -11.3 / -10.2 | -18.1 / -16.5 | -26.8 / -25.1 |
| NAM Klon clone | -2.4 | -5.5 | -10.3 | -19.2 |
| model Centaur | -2.9 | -8.6 | -15.6 | -26.2 |
| NAM JHS Bender | -6.3 | -12.7 | -19.6 | -27.4 |
| model Tone Bender / DS-1 / Big Muff | -3.2 / -2.7 / -7.8 | -7.5 / -4.9 / -8.9 | -10.4 / -7.8 / -11.7 | **-12.9 / -11.7 / -17.2** |

* TS808, RAT, OCD, Zendrive, Blues Breaker and Centaur are inside the range of the real captures, and the odd-harmonic decay of a sine through the RAT is the
  same as through the hard-drive captures (3rd..19th: -10, -16, -20, -24, -30, -36 dB against -10, -15, -20, -23, -29, -38): **the static clipper is not what makes it harsh**.
* DS-1, Tone Bender and Big Muff have 12-15 dB more 8-16 kHz than the closest captures.
* A NAM AMP capture has the speaker in it: Marshall 1959 (amp+cab) is at -19 dB (4-8k) and -45 dB (8-16k); the app's pedal-only chain has no speaker. With the Cab block
  (Celestion V30 IR) the models fall to the same range (RAT -24 / -54, TS808 -18 / -30).
* The amp models without a cab are as bright as the amp-only capture of a Bassman (8-16k -8.5 vs -11 dB), i.e. faithful, and equally in need of a cab.
