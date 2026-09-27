# RAT-Style Distortion

Display name **"RAT-Style Distortion"** (the original, single-op-amp ProCo RAT). Registry key `RatStyleDistortion`, class
`RatStyleDistortionProcessor`. Runs on [`NodalCircuit`](./NodalCircuitSolver.md), one block per channel, with the LM308 as
`addOpAmpMacro`. 2x oversampling at the Normal tier (`Orders {0, 1, 2}`, like the BD-2 and HM-2: the clipper sits in front of a
lot of gain). Cost 1.9% of a core at the default quality.

One class, three registered models: **RAT-Style Distortion** (this document's circuit, Rev P values), **RAT 2-Style Distortion** and
**Turbo RAT-Style Distortion** (keys `RatStyleDistortion`, `RAT2StyleDistortion`, `TurboRatStyleDistortion`; see "Versions" below).

## Source
The reverse-engineered schematic "RATS.UTSCH - SHEET" (Dirk Hendrik, rev P, 2006, in the GitHub `guitar-effects-schematics`
collection: `Proco Rat.pdf`), read at 2x, cross-checked with the older drawing (`Proco Rat 2.pdf`, which has a BF245A input
buffer and the same op-amp stage, clipper and filter with the same values). The schematic itself warns that it "was reverse
engineered from originals ... possible errors".

## Topology
1. **Input**: 22 nF -> node A (1M to the 4.5 V bias) -> 1K -> the LM308's (+) pin, with 1 nF from the pin to ground.
2. **Gain stage** (LM308, 30 pF compensation, non-inverting): the (-) pin's leg to ground is **360 ohm + 4.7 uF in parallel with
   47 ohm + 2.2 uF** (two shelves at 94 Hz and 1.5 kHz); feedback = the **Distortion pot (150K log, a rheostat)** || 100 pF.
   Gain = 1 + Rf / Z(leg): unity at DC, up to **~3600x (71 dB)** at the maximum. The op-amp's gain-bandwidth product (1 MHz
   with the 30 pF) then decides the bandwidth: ~280 Hz at the maximum gain, 1 kHz at a gain of 1000 -- **the RAT's sound is
   this op-amp running out of gain**, not the diodes.
3. **Clipper**: 1K + 4.7 uF -> two silicon diodes back to back to ground.
4. **Filter**: the 100K log rheostat + 1K6 into 3.3 nF to ground: 30 kHz (open) ... 475 Hz (closed).
5. **Output**: 22 nF into the gate of a 2N5458 source follower (1M to ground; an ideal follower here, drop -2.6 V, its
   bias), 1 uF, 10K, the Volume pot (100K log).

## Model
* LM308: open-loop gain 3e5, GBW 1 MHz (30 pF), 100 ohm output, swing 1.5-7.5 V (`addOpAmpMacro` with its windup clamp).
* 1N914-class silicon diodes: the project's 1N4148 model. Pots: Distortion 150K x knob^2 (audio taper: more resistance = more
  gain), Filter 100K x knob^2 (more resistance = darker), Volume knob^2.
* The bias divider (100K/100K + 1 uF) is an ideal 4.5 V; the 47 ohm / 100 uF supply filter and the 1N4001 are not modelled (ideal 9 V).

## Verification (`Tests/RatStyleDistortionProcessorTests.cpp`)
* DC: op-amp output at 4.5000 V, diode node at 0 V.
* **Small-signal gain vs the closed form** (input filter, LM308 pole, two-shelf leg, feedback || 100 pF), 4 Distortion settings x
  100 Hz / 1 kHz / 4 kHz: within 0.25 dB, except -0.56 dB at 4 kHz with the op-amp at its bandwidth limit (1x sample rate).
  At the maximum: 474x at 100 Hz, 1237x (62 dB) at 1 kHz, 230x at 4 kHz.
* Clip level 0.62 V on the diodes (silicon), symmetric to 3%.
* **No clip-recovery delay**: the op-amp output leaves the rail 44 us after the input crosses (one sample plus the bandwidth),
  which is what the windup clamp is for.
* Filter: 5 kHz/200 Hz ratio 4.06 (open) -> 0.45 (closed); Volume monotonic; no failed solves at any Distortion setting under a
  hot sine; stereo.
* Unity level: registry trim -3.05 dB.

## Not modelled -- the gap that matters here
* **The LM308's slew rate (0.3 V/us)** and its input pair's limiting. A hard-driven RAT's op-amp output is a slew-limited
  trapezoid, not a fast edge; the linear-bandwidth macro lets the edges go as fast as the gain-bandwidth product allows,
  which is faster than the real part when the differential input is large. The result is a somewhat brighter, buzzier top
  end than the real pedal at high Distortion. Fixing it needs an input-pair-limited transconductor in the macro (a new
  solver device); until then this is documented as an approximation. **Listen at high Distortion first.**
* The input JFET buffer of the older version, the 2N5458's real gain (~0.95) and distortion, the LED bypass circuit.

## Versions: RAT 2 and Turbo RAT (added 2026-09-26)
The circuit is one topology; what differs between the pedals is a handful of values. The source is the **Effects Layouts "RAT Project"**
(David Gehring / Scott Burnham, 2015; a schematic drawn for one PCB that builds the RAT, RAT 2, Turbo RAT and You Dirty RAT, verified by a
second builder, with a bill of materials column per version), read from the PDF's page 2. Cross-checked against two vero layouts (Tagboard
"Proco Rat 2", mirosol; Dirtbox "ProCo Turbo Rat") and the 1980s-style drawing in the collection (`Proco Rat 2.pdf`, BF245A buffer, 150K):
all four agree on the shared values (22 nF, 1K, 1 nF, 100 pF, 30 pF, 4.7 uF, 2.2 uF, 47 ohm, 1.5K, 3.3 nF, 22 nF, 1M gate resistor, 10K).

| Part (Effects Layouts designator) | Original (Rev P, as modelled above) | RAT 2 | Turbo RAT |
|---|---|---|---|
| R4, input node to the 4.5 V bias | 1M | **2M2** | **2M2** |
| R5, jack to ground (not modelled: the source is ideal) | -- | 2M2 | 2M2 |
| R7, leg 1 (with 4.7 uF) | 360 (Rev P; the BOM says 560) | **560** | **560** |
| Distortion pot | 150K log (Rev P; the BOM says A100k) | **A100k** | **A100k** |
| R10, in series with the Filter pot | 1K6 (Rev P; the BOM says 1.5K) | **1.5K** | **1.5K** |
| C13, after the source follower | 1 uF | **10 uF** | **10 uF** |
| Clipping diodes | 1N914 | 1N4148 (same model) | **5 mm red LEDs** (1.78 V at these currents) |
| Op-amp | LM308, 30 pF | LM308, 30 pF | **OP07** (BOM; the vero layout says "LM308 or OP07") |

* **The original is not changed.** Its three Rev P values (150K, 360, 1K6) are the open decision from 2026-09-23: three sources say 100K, the
  BOM says 560 and 1.5K. The RAT 2 and Turbo use the BOM's values (the values every source that lists them agrees on), so the RAT 2 is *not*
  "the original with two capacitors changed": its maximum gain is 1354x at 1 kHz against the original's 1237x (100K but 560 ohm in the leg,
  where Rev P has 360). If the original is later corrected to the BOM values, the three models differ exactly in the table's bold cells.
* **Turbo, op-amp.** With the OP07 (0.6 MHz gain-bandwidth, 106 dB, ~75 ohm) the gain-bandwidth is 0.6x the LM308-with-30 pF's, so the
  high-gain response is darker (773x at 1 kHz at maximum, against 1354x for the RAT 2; 140x against 232x at 4 kHz). The 30 pF (C8) is on
  pins 1/8, which on an OP07 are the offset-null pins: it does nothing there and is left out. Choosing OP07 is the BOM's call; if the real
  Turbo the user has used an LM308, only the macro in `RatStyleDistortionProcessor::specFor` changes.
* **Turbo, "220K".** The Tagboard note turns a RAT 2 into a Turbo by "swapping the 2M2 resistor for 220K"; the BOM keeps 2M2 in both. The
  input resistor only sets the input-cap high-pass (3.3 Hz with 2M2, 33 Hz with 220K) and the source impedance the pickup sees; neither
  is audible here (the input is an ideal source), so the BOM's 2M2 stands. Recorded so a listening difference is not chased there.
* Not modelled, as for the original: the LM308/OP07's 0.3 V/us slew rate, the JFET follower's real gain and distortion (an ideal follower,
  drop -2.6 V), the supply filter and reverse-polarity diode (ideal 9 V), R14 (10K across the Volume pot on the RAT 2 / Turbo) is
  already the 10K to ground the model has after the coupling capacitor.

### Verification (`RatFamilyVersionTests` in `Tests/RatStyleDistortionProcessorTests.cpp`)
* DC as the original (op-amp at 4.500 V, diode node at 0 V).
* Op-amp stage gain against the closed form (2M2 input node, 560/47 ohm two-shelf leg, 100K rheostat || 100 pF, each model's own A0 and
  gain-bandwidth), 4 Distortion settings x 100 Hz / 1 kHz / 4 kHz: within 0.22 dB up to 1 kHz, -0.5 dB at 4 kHz where the op-amp is at its
  bandwidth limit (1x sample rate) -- the same tolerance and pattern as the original.
* Clip level: RAT 2 +-0.624 V (silicon), Turbo +-1.783 V (LEDs), symmetric.
* Bounded and converging under a hot sine at every Distortion setting; stress test (random knobs, plucked notes, hot bursts) with no failed
  solves.
* Unity trims (registry): RAT 2 +1.25 dB, Turbo -7.85 dB (`PedalUnityLevelTests`).
* Aliasing (non-harmonic / harmonic, 1x / 2x / 4x / 8x): original -25.6 / -32.3 / -44.8 / -69.9 dB, RAT 2 -25.9 / -33.0 / -45.0 / -70.7,
  Turbo -34.9 / -42.2 / -58.3 / -79.9 (the LEDs clip later and the OP07 has less bandwidth). Tiers `{0, 1, 2}` for all three, like the
  original. Cost as the original (~1.9% at 1x).
