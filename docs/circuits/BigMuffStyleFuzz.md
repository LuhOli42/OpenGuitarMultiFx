# Big Muff Pi-Style Fuzz (USA V3, Russian "Green" and Sovtek 1st edition)

`BigMuffStyleFuzzProcessor` — one class, three registered models: `BigMuffStyleFuzz` (USA Version 3),
`RussianBigMuffStyleFuzz` (Version 7B/7C, the all-green Civil War / Tall Font Russian) and `SovtekBigMuffStyleFuzz`
(Version 7 1st edition, the first Russian-made Big Muff, 1990-91). USA vs Russian: same topology, different values
throughout; the same one-class-many-models pattern the Tube Screamer family already uses.

**The Russian and the Sovtek are one circuit.** Kit Rae's own text: the Version 7 Big Muffs were "the Russian made
Sovtek Big Muffs" (the Green Civil War *is* a Sovtek Big Muff Pi), in "3 circuit variants" whose only difference is
the feedback/filter caps: 430 pF (1st edition, also sold as the Red Army Overdrive), two 1 nF in series = 500 pF
(Civil War / Tall Font), a single 470 pF (bubble font and the Black Russian V8). Every other value on the three
traces is the same. So `sovtekFirstEdition` is the Russian model with one number changed (430 pF), and it measures
like it: unity gain 9.72 dB against 9.71 dB. That cap is the feedback low-pass corner of each stage with its 470k
(787 Hz at 430 pF, 677 Hz at 500 pF), so the 1st edition is a touch brighter. The Black Russian (470 pF) is not
registered separately: it would sit between the two by the same argument.

**A second source for the Russian values.** Tonepad's "Gran Mango" schematic of the *Green Big Muff* (12k collectors,
390 ohm emitters, 0.047 uF in series with the diodes, 0.1 uF couplings, 22k / 0.0039 uF / 20k tone, 470k/10k output
stage) agrees with Kit Rae's trace on every value used here; the one difference is the output stage's emitter
resistor, 2k there against 2.7k on Kit Rae's trace (the model uses 2.7k). Its "Stock" schematic also agrees with the
USA model apart from C1 (1 uF there, 10 uF on the V3 trace).

**Display names** are "Big Muff-Style Fuzz" and "Russian Big Muff-Style Fuzz" — Big Muff Pi® is an active
Electro-Harmonix trademark, so the project's standing "-Style" convention applies (see
`PositiveGroundBooster.md`'s trademark note for the full reasoning).

## Sources
Two circuit traces by Kit Rae (bigmuffpage.com), both drawn from real units by the same author in the same
notation, which is what makes them directly comparable:
* **USA V3**: `KR_1976_V3_No3_schematic.jpg`, a 1976 V3 ("76#3"), captioned as *the most common circuit found in
  the V3 Big Muff*. BC239 silicon transistors, 1N914 diodes.
* **Russian**: `V7BMP_Schematic_GreenCivilWar_TallFontRussian.jpg` (V7B/V7C). Unmarked Russian NPN; Kit Rae gives
  2N5089 as the modern equivalent.

A third drawing (`yuvadm/guitar-effects-schematics`, "Electro Harmonix Big Muff Pi.gif", captioned "the original
schematic") was read first and then **not** used: it disagrees with the traced V3 on many values (C1 1 µF vs
10 µF, R14 100k vs 47k, the stage-input resistors 8.2k vs 10k, the output stage 10k/2.2k vs 15k/3.3k) and it omits
the third stage's 100k base resistor entirely. Same lesson as the DS-1: where a clean trace of a real unit exists,
it beats a redrawn "original" — see that doc's own account of three misreadings found the same way.

## What the circuit is
Four NPN stages in a row. Stages 1-3 are the *same* cell: a common-emitter amplifier biased by shunt feedback (a
470k from collector back to base), with a small cap across that feedback resistor. Stage 4 is a plain
divider-biased recovery stage after the tone control.

Signal order is **the reverse of the schematics' part numbers** — Kit Rae numbers Q4→Q1 from input to output,
because that is how they are silkscreened on the real PCB. This model numbers them in signal order (stage 0-3), and
`debugCollector(stage)` uses signal order too.

```
in --R2-- C1 --+-- [stage 1] --C4--> SUSTAIN pot --C5--R19--+-- [stage 2, clipping] --C13--R12--+
               |                            (R23 1k below)  |                                  |
              R14                                          R20                                R16
                                                                                                |
    +-- [stage 3, clipping] --+--C9--+-- TONE pot --+--C3--> [stage 4] --C2--> VOLUME --> out
    |                         |      (R5 to gnd)    |
    |                         +--R8--+ (C8 to gnd) -+
```

### The clipping stages are the whole point
Each clipping stage bridges base to collector with **a capacitor in series with an anti-parallel silicon diode
pair**, in parallel with the 470k bias/feedback resistor and its small cap. Two consequences, and both are what a
Big Muff sounds like:

* The diodes are in the **feedback** path, not shunting the signal to ground (that is the DS-1 / Distortion+
  arrangement, see `DistortionPlusStyleDistortion.md`). As they start conducting, the stage's own gain collapses
  smoothly instead of the waveform hitting a wall — a soft, progressive squash that keeps going as the note decays.
  That is the "sustain" the knob is named after.
* The series capacitor blocks DC, so the diodes only ever see signal and the stage's bias point is untouched by
  them. It also sets **which frequencies get clipped**: below its corner the diodes barely conduct and the bass
  passes through comparatively clean. This one capacitor is the single biggest difference between the two models
  (1 µF USA vs 0.047 µF Russian).

### The tone stack scoops the middle by construction
A treble branch (a small cap into a resistor to ground) and a bass branch (a resistor into a cap to ground) are
driven in parallel from stage 3, and the pot picks a point between them. There is no setting that fills in the
middle — the notch between the two branches is always there. That is the other half of the Big Muff sound, and the
reason a Big Muff famously disappears in a band mix.

## The two models, side by side
Only the values differ; every node is the same.

| | USA V3 | Russian V7B/7C | what it does |
|---|---|---|---|
| Collector load (stages 1-3) | 15k | **12k** | with the emitters below: much less gain per stage |
| Emitter, stage 1 | 100 Ω | **390 Ω** | |
| Emitter, clipping stages | 150 Ω | **390 Ω** | open-loop gain per stage drops from ~100 to ~31 |
| Cap in series with the diodes | 1 µF | **0.047 µF** | the Russian clips far less bass -> "fatter bottom end" |
| Feedback/filter cap | 470 pF | 500 pF (two 1 nF in series) | |
| Input coupling C1 | 10 µF | **0.1 µF** | |
| Input stage base resistor R14 | 47k | **100k** | moves that stage's operating point a lot (see below) |
| Tone bass-branch resistor R8 | 39k | **20k** | different corner, different notch |
| Tone treble cap C9 | 0.004 µF | 0.0039 µF | |
| Output stage | 430k/100k, 15k, 3.3k | 470k/100k, **10k**, **2.7k** | |

**Why the Russian's gain really is lower, not just nominally:** a shunt-feedback stage can only deliver the gain
its open loop can support. Ideal closed-loop here is 470k/10k ≈ 47 for both models, but the open-loop gain is
roughly Rc/Re — about 100 for the USA (15k/150) and about **31 for the Russian** (12k/390), *below* what the
feedback is asking for. So the Russian's stages run with weak feedback and less gain, which is exactly the
"less gain, fatter bottom end" every description of these pedals reports.

## Verification (`Tests/BigMuffStyleFuzzProcessorTests.cpp`)
* **DC operating point, all four stages** — the test that catches a transistor silently stuck at cutoff or
  saturation, which a boundedness test cannot (the lesson from the booster's cutoff bug). Hand-derived targets vs
  measured collector voltages:

  | stage | USA predicted | USA measured | Russian predicted | Russian measured |
  |---|---|---|---|---|
  | 1 (input) | 6.77 V | **6.90 V** | 3.83 V | **4.69 V** |
  | 2 (clip) | 3.83 V | **4.20 V** | — | 4.69 V |
  | 3 (clip) | 3.83 V | **4.14 V** | — | 4.60 V |
  | 4 (output) | 4.0 V | **4.41 V** | 5.37 V | **5.68 V** |

  The hand derivation ignores base current and the emitter drop, so it reads slightly low throughout; the two
  clipping stages are also required to agree with each other to within 0.25 V, since they are the same cell.
* **Sustain compresses, it does not get louder.** Measured, 4x input change (0.05 -> 0.2 V, soft picking to
  digging in): USA 1.21x -> **1.01x** output change from Sustain 0 to Sustain 1; Russian 1.40x -> **1.18x**. That
  near-1.0 is the Big Muff's signature and the test asserts it. A level-based test was written first and was
  wrong: over the whole Sustain sweep the USA's output only moves 0.76 -> 0.92 p-p, because both clippers are
  already into their diodes either way.
* **Transfer curve** (dev only, `BMP_CURVE=1`): USA at full Sustain, input 0.01 -> 0.8 V (80x) gives output
  0.885 -> 0.961 p-p, a 1.09x change. The Russian stays compressed over the guitar range and opens up again above
  ~0.4 V input, which is booster territory, not a guitar — its lower-gain stages stop clipping flat there. Honest
  behaviour of the values, not a modelling artefact.
* Tone sweeps the treble/bass balance (USA 0.38 -> 6.38, Russian 0.30 -> 3.22), Volume is monotonic and near-silent
  at zero, and random knobs with 3 V hot bursts stay finite with a **0.000000 solver failure rate**.
* **Steady-state clean** (`OversampledEffectTests`): non-periodic error **-141.8 dB** (USA) and **-430.0 dB**
  (Russian), against the -80 dB bar. No solver glitches, no noise floor.

## Cost and oversampling
One `NodalCircuit` block (every stage loads the one before it through its coupling cap, so there is no node that
cannot be loaded back and therefore no valid cut). Four two-port BJTs plus two anti-parallel diode pairs.

* **1.28 (USA) / 1.30 (Russian) Newton iterations per sample** — cheap, the circuit is well-conditioned.
* **7.32% / 7.17% of a core** at 1x (dev PC, dual-mono input), next to the HM-2's 6.39%. Above the project's 5%
  per-pedal target, in the same bracket as the other four-device netlists.
* Aliasing (non-harmonic/harmonic, the method in `Oversampling.md`):

  | | 1x | 2x | 4x |
  |---|---|---|---|
  | USA | -21.1 dB | -41.0 dB | -47.1 dB |
  | Russian | -26.1 dB | -46.3 dB | -69.0 dB |

  -21 dB at 1x is the worst 1x figure of any pedal in the project (the DS-1's is -17.8), so the quality tiers are
  **1x / 2x / 4x** (eco / normal / high), the same as the DS-1 and HM-2.

## Unity level
Measured gain at noon for the reference signal: **+5.42 dB** (USA) and **+9.71 dB** (Russian); the trims in
`EffectRegistry.cpp` are the negatives of those. Re-measure after any circuit change — `PedalUnityLevelTests`
prints the new number.

## Assumptions and what is not modelled
* **Transistor parameters are assumed**, as in every other pedal here: Is = 1e-14, Vt = 25.85 mV, betaR = 4, with
  betaF **400** for the BC239-class USA parts and **500** for the 2N5089-class Russian ones. Real units vary
  enormously (these are deliberately unmatched, hand-sorted parts), and the shunt-feedback bias makes the operating
  point forgiving of beta, which is why the circuit tolerated unmarked transistors in the first place.
* Diodes are the project's standard 1N914/1N4148 set (Is = 2.52 nA, N = 1.752). The real pedals' diodes were
  whatever was in the bin; V3s are generally 1N914.
* The 9 V rail is ideal. The supply filter cap (C14) and its series resistor are not modelled — unlike a tube amp,
  nothing here sags meaningfully.
* The output sees an assumed 1 MΩ amplifier input, the same assumption the DS-1 model makes.
* Not modelled: the bypass switching, the LED and its resistor, and (on the Russian) the fact that the pots were
  sometimes 150k instead of 100k.
