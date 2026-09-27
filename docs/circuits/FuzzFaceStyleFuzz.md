# Fuzz Face-Style Fuzz (germanium and silicon)

`FuzzFaceStyleFuzzProcessor` — one class, two registered models: `FuzzFaceStyleFuzz` (germanium, PNP, positive
ground: the original Dallas Arbiter) and `SiliconFuzzFaceStyleFuzz` (NPN, the later silicon version). Same
topology; the values that change are the transistors and one resistor.

**Display names** "Fuzz Face-Style Fuzz" / "Silicon Fuzz Face-Style Fuzz": Fuzz Face® is a registered trademark,
so the project's standing "-Style" convention applies (`PositiveGroundBooster.md` has the reasoning).

## Sources
* **Schematics**: General Guitar Gadgets' two drawings, `ggg_ff5_sc_pnp.pdf` (germanium) and `ggg_ff5_sc_npn.pdf`
  (silicon). Only R4 differs between them (470 ohm vs 330 ohm); every other value is the same on both.
* **Analysis, used as the check**: R.G. Keen, "The Technology of the Fuzz Face" (Geofex).
* Two other drawings were read first and set aside. The `yuvadm` collection's "Fuzzface.pdf" is the same circuit but
  was ambiguous about where the output capacitor attaches, and its "Fuzz Face Derivant" is a different design
  (2N3904s, 47k, a bias trimmer). Neither is evidence about the original.

### A reading that looked wrong and is right
In every drawing the output capacitor C3 hangs off the top of R5 (the 8.2k), on the supply side of R4, not off the
transistor's collector. That looks like a drawing error — a capacitor on a node the supply holds at AC ground
should carry no signal. It is not: Geofex describes it directly. Q2's collector load is **split** (R4 + R5, 470 +
8.2k) and the output is taken at the junction, "like a volume pot permanently set to a low value". The junction only
sees the signal current times R4, so the pedal delivers roughly R4/(R4+R5) — about 1/18 (germanium) or 1/26 (silicon)
of the swing at the collector. That is deliberate ("not much larger than the input signal, to keep the huge amount
of signal available from overdriving the amp") and it is why this pedal is naturally quiet: the unity trims are
+10.7 dB and +14.5 dB, the largest in the project.

## What the circuit is
```
in --Rsrc-- C1 2.2uF --+-- Q1 base                 Q1 collector == Q2 base (direct coupling)
                       |                            R2 33k: supply -> Q1 collector
                     R3 100k                        Q2 emitter -> FUZZ pot 1K (wiper bypassed to ground by 22uF)
                       |                            R3 100k: Q2 emitter -> Q1 base
                 Q2 emitter                         supply -> R4 -> [x] -> R5 8.2k -> Q2 collector
                                                    [x] -> C3 0.01uF -> VOLUME 500K -> out
```

* **Direct coupling and one resistor do everything.** Q1's collector *is* Q2's base. R3 takes Q2's emitter back to
  Q1's base, which is at once the DC bias for both transistors (voltage-feedback bias: a self-centring loop, no
  divider) and the AC feedback that sets the gain.
* **The Fuzz pot changes AC gain, not DC.** Its whole 1K is the DC emitter resistance whatever the wiper does; the
  22 uF on the wiper shorts the lower part for signal. So the two segments always sum to 1K (in the code, both
  resistors move together), and turning Fuzz up takes degeneration out of Q2 rather than moving the bias.
  Geofex: "the gain can vary from a low of about 8 to as high as the transistor's basic internal gain".
* **Very low input impedance.** Feedback from an emitter follower into a base gives a low input impedance, so
  "the base can only move a few tens of millivolts" and the pedal loads the guitar heavily. That interaction is a
  large part of what a Fuzz Face sounds like and why it cleans up with the guitar's volume knob. See the next
  section for what this model does about it.
* **Saturation is mushy** (Geofex): a large input drives Q1's collector toward its emitter, which lowers the bias
  current fed back through R3 and takes drive away from the input. Nothing in the code does that on purpose — it
  falls out of the netlist.

### Modelling decision: the guitar is part of the circuit
Every other pedal in this project reads its input as an ideal voltage source (`GainStaging.md`). That is fine for
an input of 500k-1M and would be wrong here: with an ideal source the low input impedance never loads anything and
the pedal loses the interaction it is known for. So this processor puts a series **6 kohm** (`guitarSourceResistance`,
a single coil's DC resistance) between the input and the pedal. Assumed, and worth knowing the edges of:
* It is a resistance only. A real pickup is an inductor (~2.5 H) with a resonance, so its source impedance *rises*
  with frequency; the model has none of that, so it does not reproduce the tone-darkening that inductance gives a
  Fuzz Face, only the level-dependent loading. Adding the pickup's inductance is the obvious next step and is not
  done because it needs values that vary by pickup by a factor of several.
* It models the guitar plugged in directly. Behind a buffer (or another pedal) a real Fuzz Face sounds different and
  this one will not, because the source resistance is a constant, not read from the chain.

## The two models
| | germanium (PNP) | silicon (NPN) |
|---|---|---|
| Supply | -9 V (positive ground) | +9 V |
| Transistors | AC128 / NKT275 class: Is 2e-7, beta 100 | BC108C class: Is 1e-14, beta 500 |
| R4 | 470 ohm | 330 ohm |
| Leakage | 2.2 Mohm collector-base on both | none |

The PNP model is built with the real polarity (a negative rail, `pnp = true`), not a mirrored NPN circuit, so the
signal polarity and the direction of the asymmetric clipping are the original's.

**Assumptions to know about:**
* Germanium Is comes from a forward drop of ~0.22 V at 1 mA. Real Ge parts differ a great deal — this pedal was
  built from hand-picked ones with a gain around 80-120 — and beta of 100 for both is a stand-in for that.
* Germanium leakage is modelled as a fixed 2.2 Mohm collector-base resistance (about 2 uA at the 4.5 V Q2 sees;
  Geofex calls "a few microamps" the limit of a usable part). Real leakage doubles every ~8-10 degrees C, which
  is why these pedals are famously temperature-sensitive; this model has none of that.
* The 9 V supply is ideal. Geofex points out the battery's own impedance adds to the 470 ohm and that the pedal
  sounds different on a worn battery; not modelled.

## Verification (`Tests/FuzzFaceStyleFuzzProcessorTests.cpp`)
* **DC operating point** (the check that catches a transistor stuck at a rail):

  | | Q1 collector | Q2 emitter | Q2 collector |
  |---|---|---|---|
  | germanium | 0.609 V | 0.412 V | 5.423 V |
  | silicon | 1.307 V | 0.663 V | 3.349 V |

  Q2 is an emitter follower for DC, so Q1's collector sits one base-emitter drop above Q2's emitter: 0.197 V
  (germanium) and 0.644 V (silicon), both right for their junctions. Hand derivation for the silicon version gave
  ~1.35 V / 0.70 V / 3.0 V. Both Q2 collectors sit mid-supply with room to swing either way.
* **Q2's stage gain with Fuzz fully down: 8.12 (germanium), 8.27 (silicon)**, measured node to node (Q2 collector
  over Q1 collector, 1 mV in, sample by sample). Geofex's independent statement is "a low of about 8"
  (8.67k collector load over the 1K pot plus Q2's own ~40 ohm).
* **Fuzz raises the gain a lot**: 5 mV in gives 0.065 -> 0.503 p-p (germanium) and 0.051 -> 0.357 (silicon) from
  Fuzz 0 to 1, 7.7x and 7.0x.
* Volume is monotonic and near-silent at zero; random knobs with 3 V hot bursts stay finite with a 0.000000 solver
  failure rate; the two models measure differently.
* **Steady-state clean**: non-periodic error -418.7 dB (germanium), -415.4 dB (silicon) against the -80 dB bar.
* Unity level and the re-`prepare()` transparency test cover both models automatically.

## Cost and oversampling
One `NodalCircuit` block, two BJTs, no diodes. **1.26 / 1.24 Newton iterations per sample and 2.4% of a core** at 1x
(dev PC, dual-mono), the cheapest fuzz in the collection and inside the 5% per-pedal target.

Aliasing (non-harmonic/harmonic, method in `Oversampling.md`):

| | 1x | 2x | 4x |
|---|---|---|---|
| germanium | -16.7 dB | -27.0 dB | -46.3 dB |
| silicon | -15.4 dB | -25.4 dB | -44.1 dB |

Worst at 1x of anything in the project (the DS-1's is -17.8, the Big Muff's -21), so tiers are 1x / 2x / 4x.
Unity trims **+10.70 dB** (germanium) and **+14.45 dB** (silicon).

## Not modelled
Bypass switching and the LED; the optional reverse-protection diode and filter cap; pickup inductance (above);
temperature; battery sag and impedance.
