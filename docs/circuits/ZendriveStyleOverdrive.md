# Zendrive-Style Overdrive

Display name **"Zendrive-Style Overdrive"** (after the Hermida Audio / Lovepedal Zendrive). Registry key `ZendriveStyleOverdrive`, class
`ZendriveStyleOverdriveProcessor`. Runs on [`NodalCircuit`](./NodalCircuitSolver.md), one block per channel (one op-amp macro; the second
op-amp is an ideal follower). 1x cost 2.8% of a core; tiers `{0, 0, 1}`. Unity trim -10.14 dB.

## Source
* **Aion FX "Azimuth" (Zendrive clone), documentation page 3** -- a clean CAD schematic with every value, drawn rotated 90 degrees (read
  rotated and at 2x). Its text: "a standard TS-style clipping section followed by a variable R-C treble cut and a buffer ... the original's
  op-amp was sanded, almost certainly an AD712; Lovepedal uses an NE5532".
* **Stomp Box Schematics, "Lovepedal Hermida Audio ZenDrive project by implex"**: a small redraw (600 px) with MC1458s; readable only for the
  topology, which agrees with the Aion drawing (a MOSFET and BAT41 diodes across the feedback, one MOSFET + diode the other way). Values
  are Aion's.
* Analog Is Not Dead / ElectroSmash have no Zendrive analysis.

## Topology
1. **Input**: 470 nF -> (+) with 470K to the 4.5 V bias (the 2M2 input pull-down is on the jack side of the ideal source: not modelled).
2. **Gain stage** (AD712, non-inverting). (-) leg: **1K + the Voice rheostat (10K) + 100 nF to the bias**. Feedback: 100 pF, the **Drive pot**
   (500K linear: pin 1 on the output, pin 3 on (-), its wiper joined to (-) through 1K, so Rf = (output-to-wiper) + (wiper-to-(-) || 1K):
   1K .. 500K), and the clipper.
3. **The clipper across the feedback** (output vs the (-) input):
   * output *below* (-): (-) -> **BAT41 -> 1N34A -> the body diode of Q1** -> output;
   * output *above* (-): output -> **BAT41 -> the body diode of Q2** -> (-).
4. **Tone**: 10K + a 50K rheostat into 3.3 nF to ground (4.8 kHz .. 800 Hz), then the second op-amp as a unity follower.
5. 470 nF, 1K, the Volume pot (100K linear).

## The 2N7000s are body diodes here (the finding that matters)
Each 2N7000 has its **gate tied to its drain**, so it is a two-terminal device: its channel (a "diode" that needs ~2 V, forward from drain to
source) in parallel with its body diode (source to drain, ~0.65 V). In both legs the series diode points **the other way** from the channel's
direction (the schematic's polarity, confirmed by the drawing's symbols including the body diode): the channel's current would have to flow
backwards through the BAT41 / 1N34A, so **it is blocked, and only the body diodes conduct**. That is why the clipping is asymmetric (Q1's leg has
the extra 1N34A) and why Aion warns that this circuit "has a particularly strong dependence on the type of op-amp": there are no
low-threshold silicon diodes in it. The same reading applies to the [OCD](./OCDStyleOverdrive.md), where Aion states the polarity rule outright
("if the germaniums are reversed in relation to the MOSFET polarity, the path to ground will be blocked").

The channels are therefore not in the netlist. Each leg is a chain of junctions carrying one current, so it is one diode
(`SeriesDiodes.h`: nVt = sum of the chain's, Is = the geometric mean weighted by nVt), and the two antiparallel legs are the solver's single
clipper port. A test compares the chain with the explicit netlist (within 1-2 mV from 1 uA up). This took the pedal from 7.0% to 2.8% of a
core with the output unchanged (-0.967 / +0.806 V both ways).

## Model
* AD712: A0 2e5, 4 MHz gain-bandwidth, 75 ohm, swing 2-7 V. The second op-amp is a follower (unity, inside its swing at these signals).
* BAT41: Is 13 nA, N 1.1 (0.25 V at 0.1 mA, 0.32 V at 1 mA; the datasheet's <0.45 V at 10 mA needs a series resistance left out).
  1N34A: the Centaur's (Is 200 nA, N 1.3). 2N7000 body diode: **Is 2 pA, N 1.4 (0.64 V at 0.1 mA, 0.73 V at 1 mA) -- an assumption**: the
  datasheet gives only V_SD < 1.5 V at 0.4 A.
* Pots: Drive and Volume linear as the original (Aion recommends audio taper), Tone and Voice rheostats **clockwise = less resistance**
  (Tone: brighter; Voice: more gain range, per Aion's "the available gain increases as the Voice control is turned up").

## Verification (`Tests/ZendriveStyleOverdriveProcessorTests.cpp`)
* DC: the op-amp output, its (-) input and the tone node at 4.500 V.
* **Gain stage against the closed form** (input high-pass, AD712 pole, 1K + Voice + 100 nF leg, Rf as above || 100 pF), Drive 0.1 / 0.5 / 1 x
  Voice 0 / 0.5 / 1 x 150 Hz / 1 kHz: within 0.09 dB (up to 262x at 1 kHz with Drive and Voice at the maximum; 47x at 150 Hz).
* **Clipper**: output minus (-) reaches **+0.806 V** (BAT41 + body) and **-0.967 V** (BAT41 + 1N34A + body): asymmetric, the negative side
  harder. These are at the currents this circuit runs (a few uA through up to 500K): at 1 mA they would be 0.32 + 0.73 = 1.05 V and 1.4 V.
  **They depend directly on the body-diode assumption**: a factor 10 in its Is moves both by 83 mV.
* Tone: 5 kHz / 200 Hz 0.240 (Tone 0) -> 1.010 (Tone 1). Voice 1 vs 0: 1 kHz level x5.6, 100 Hz relative to 1 kHz x0.2. Volume monotonic.
* Series-diode equivalence, bounded/converging at every Drive setting, stress with random knobs (no failed solves), stereo independence.
* Aliasing (non-harmonic / harmonic, 1x / 2x / 4x / 8x): -40.2 / -54.0 / -74.8 / -96.3 dB (the low-gain, soft-ish feedback clipper is clean at 1x).

## Not modelled / assumptions
* The Zendrive's op-amp is disputed (AD712 / NE5532 / MC1458 / TC2272): "Zendrive" sounds are said to depend on it; the macro is the AD712.
* Aion's "RPD" (1M to 2M2, the jack's pull-down), the LED and the supply filter (D1 1N4002, 47 uF, 10K/10K bias divider) are not audio-path
  elements here (ideal 9 V and 4.5 V).
* Some real Zendrives use a second BAT41 in place of the 1N34A ("Clipping diode" footnote): the negative threshold would then be ~60-100 mV lower.
