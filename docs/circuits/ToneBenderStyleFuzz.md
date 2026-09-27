# Tone Bender Mk II-Style Fuzz (germanium and silicon)

`ToneBenderStyleFuzzProcessor` — one class, two registered models: `ToneBenderStyleFuzz` (germanium, PNP, positive
ground: the original three-transistor Colorsound "Professional") and `SiliconToneBenderStyleFuzz` (NPN, with the
bias network a silicon part needs).

**Display names** "Tone Bender-Style Fuzz" / "Silicon Tone Bender-Style Fuzz": Tone Bender® is a trademark, so the
project's "-Style" convention applies (`PositiveGroundBooster.md` has the reasoning).

## Sources
General Guitar Gadgets' two Mk II Professional drawings (`ggg_tb_m2p_sc_pg.pdf` germanium, `ggg_tb_m2p_sc_ns.pdf`
NPN silicon). The `yuvadm` collection's "Vox Tone Bender" was read first and **not** used: it is a two-transistor
circuit that its own caption calls "very similar to Fuzz Face" (same directly coupled pair, same split load, a 47k
feedback), so it would have been the Fuzz Face again under another name. The Mk II is the one with a third stage, and
that stage is what makes it a different circuit.

## What the circuit is
```
in --Rsrc--+-- C1 4.7uF --+-- Q1 (CE, 10k) --C2 0.1uF--> Q2 base ... Q2 collector == Q3 base (direct)
           |  (R9 1M5 and C6 0.01uF to ground)                Q3 emitter -> R4 100k -> Q2 base
           R1 100k to Q1 base                                Q3 emitter -> ATTACK pot (wiper bypassed by 4.7uF)
                                                             supply -> R5 1k -> [x] -> R6 8k2 -> Q3 collector
                                                             [x] -> C3 -> VOLUME 100k -> out
```
* **Q2 + Q3 are a Fuzz Face** (`FuzzFaceStyleFuzz.md`): a directly coupled pair biased by one resistor from the
  second transistor's emitter back to the first's base, that resistor doubling as AC feedback; the Attack pot's
  wiper is bypassed, so it moves the gain and not the bias; the collector load is split (1k + 8.2k) with the output
  at the junction.
* **Q1 is the addition**: one more common-emitter stage in front, AC-coupled by 0.1 uF. It buys a great deal more
  gain and more compression, and it changes the input: the guitar sees C6 (0.01 uF) to ground, so with the
  guitar's source resistance that is a low-pass with its corner near 2.7 kHz (6k x 0.01 uF). That one cap, together
  with the source resistance, is a large part of how a Tone Bender takes the fizz off the top.
* The output is very quiet by design, as on the Fuzz Face (the junction sees only the signal current times R5).

### Attack is a reverse-log pot
`R7` is "1k reverse log". The segment that degenerates Q3 (top of the pot, for signal) is `audio(1 - attack)` of the
track: at half rotation it is already 15% (150 ohm), and Q3's stage gain is roughly 8.7k over (Rdeg + re). The result
is a steady climb over the whole knob — Q3's stage gain works out (computed from the pot law, not measured) to
about 8.7 / 20 / 45 / 100 at 0 / 25 / 50 / 75% rotation — where a linear pot would leave most of the sweep in the
low-gain region and pile the change into the last few degrees.

## The germanium version has no bias network for Q1
This is the thing the two drawings tell you. On the germanium one, **Q1's base goes to ground through a 100k and
nothing else**. No divider, no feedback resistor. It works because a germanium transistor leaks: current through the
reverse-biased collector-base junction flows out of the base through that 100k and pulls the base negative, until the
emitter junction turns on and supplies the rest. The circuit is built around a leaky part. The silicon drawing, by
the same author, has to *add* what the germanium one gets for free: a 470k collector-base resistor (R10), a 2.7k
emitter resistor on Q1 (R11), 100 ohm on Q2 (R12), 220 ohm on Q3 (R13), and a 20k trimmer for Q3's collector load
(R6T) that the builder sets to centre the collector.

So the germanium model needs an explicit **collector-base leakage** resistor on each transistor, and Q1's operating
point *depends on its value*: 
* Hand derivation for Q1 (base at ~-0.18 V, KCL on the base with R1 and the leakage): with 1 Mohm, Ic1 ~= 350 uA and
  the collector sits at ~-5.5 V; 2.2 Mohm gives ~150 uA (near the rail); 500k gives ~530 uA (-3.7 V).
* The model uses **1 Mohm**. Measured: Q1 collector **-5.506 V** against the hand-derived -5.5 V.
* This is the honest weak point: the leakage is what a builder selected transistors for ("the parts were hand-sorted"),
  and it is an assumption here. A leakier part than 1 Mohm pulls Q1's collector toward the rail and its gain down;
  a tighter one cuts Q1 off. It also doubles every 8-10 degrees C, which this model does not.

## The two models
| | germanium (PNP) | silicon (NPN) |
|---|---|---|
| Supply | -9 V (positive ground) | +9 V |
| Transistors | OC81D / OC75 class: Is 2e-7, beta 100 x3 | 2N4401 (Q1, Q2) beta 200; 2N5089 (Q3) beta 600; Is 1e-14 |
| Q1 bias | leakage only (1 Mohm collector-base) | R10 470k collector-base, R11 2k7 emitter |
| Q2 / Q3 emitter resistors | none / none (straight to ground / the pot) | R12 100 ohm / R13 220 ohm |
| Q3 collector load | R6 8.2k | R6T 20k trimmer, **set to 5.4k** (below) |
| C3 | 0.01 uF | 0.1 uF |

The PNP model uses the real polarity (a negative rail, `pnp = true`), not a mirrored NPN circuit.

**The silicon trimmer** is an adjustment the builder makes with a meter, so it is set the same way here. At the
trimmer's midpoint-ish 12k, Q3's collector sat at 0.86 V, 0.04 V above its own emitter: saturated, and silent. Q3's
current is set by its emitter network (~0.86 V over 220 ohm + 1K = 0.70 mA), so centring the collector at 4.5 V needs
R5 + R6 = 4.5 V / 0.70 mA => R6 = 5.4 k, well inside the 20k range. Measured with 5.4k: **4.508 V**.

## Verification (`Tests/ToneBenderStyleFuzzProcessorTests.cpp`)
* **DC operating point** (the check that catches a stage stuck at a rail), magnitudes:

  | | Q1 collector | Q2 collector | Q3 emitter | Q3 collector |
  |---|---|---|---|---|
  | germanium | 5.506 V | 0.511 V | 0.320 V | 6.019 V |
  | silicon | 6.940 V | 1.502 V | 0.857 V | 4.508 V |

  Q3's base is Q2's collector, so it sits one junction above Q3's emitter: 0.191 V (germanium), 0.645 V (silicon).
* **Attack raises the gain a great deal**: at 0.1 mV in, 0.093 -> 0.900 p-p (germanium, 9.6x) and 0.0054 -> 0.0713
  (silicon, 13x) from Attack 0 to 1. The test uses 0.1 mV on purpose: an earlier 2 mV version saturated the
  germanium model at Attack 0 already (1.09 V p-p, the Q3 collector sweeping the whole supply). This circuit's gain
  is enormous — about 4300x from the source voltage to Q3's collector at minimum Attack for the germanium version (derived
  from that run's 0.093 V p-p at the output junction) — and the test has to be below that to see the knob.
* Volume is monotonic and near-silent at zero; random knobs with 3 V hot bursts stay finite with a **0.000000**
  solver failure rate on both models; the two models measure differently.
* **Steady-state clean**: non-periodic error -425.7 dB (germanium), -428.0 dB (silicon), against the -80 dB bar.
* Unity level and the re-`prepare()` transparency test cover both models automatically.

## Cost, oversampling and the aliasing finding
One `NodalCircuit` block, three BJTs. **1.33 / 1.35 Newton iterations per sample; 4.2% (germanium) and 3.9% (silicon)
of a core at 1x.**

**This is the worst-aliasing circuit in the project**, and the reason is the gain. Non-harmonic/harmonic energy
(method in `Oversampling.md`):

| | 1x | 2x | 4x | 8x |
|---|---|---|---|---|
| germanium | -15.5 dB | -22.2 | -28.6 | -35.1 |
| silicon | -14.9 dB | -20.4 | -26.9 | -35.2 |

A stage with ~70 dB of gain clips into something close to a square wave, whose harmonics fall as 1/n, so every
doubling buys only ~6-7 dB (the Big Muff and Fuzz Face, which clip more softly, gain 20 dB from 1x to 2x). Even 8x
does not reach what the other fuzzes have at 4x. The tiers are therefore **eco 1x / normal 4x / high 8x**, skipping
2x, which is not worth its cost here. At 4x the pedal should cost about four times the 1x figure (an estimate: the 1x number is measured, the oversampled
ones were not). This is the
sharpest example so far of the pressure the user warned about ("high-gain models will make the solver math the
limit"): the solver is cheap (1.3 iterations); the *oversampling* is what a hard-clipping fuzz costs.

Unity trims: **+6.14 dB** (germanium), **+1.29 dB** (silicon).

## Not modelled
Bypass switching and the LED; the 47 uF supply decoupling cap (the supply is ideal, so it would sit across an ideal
source); pickup inductance (see `FuzzFaceStyleFuzz.md`); temperature and battery impedance; and the parts' real
spread, which for a Tone Bender is a large part of why two of them sound different.
