# OCD-Style Overdrive

Display name **"OCD-Style Overdrive"** (after the Fulltone OCD, version 1.7). Registry key `OCDStyleOverdrive`, class
`OCDStyleOverdriveProcessor`. Runs on [`NodalCircuit`](./NodalCircuitSolver.md), **two blocks per channel**, cut at the clipper node (the
second op-amp's (+) input draws nothing). 1x cost 5.3% of a core; tiers `{0, 1, 2}`. Unity trim -14.37 dB.

## Source
* **Aion FX "Titan Dynamic Drive" documentation, page 8** (schematic) and pages 3-7 (parts list, DIP-switch table, version history): a
  clean CAD schematic with every value. **Its default parts list is v1.7, the version in production longest (~75,000 units)**, and its DIP
  switches reproduce the differences between v1.1-v2.0. This is the source of the model.
* **Analog Is Not Dead, "Circuit Analysis: Fulltone's OCD"**: a redrawn schematic with a simulation. Its gain-stage values agree with Aion's
  (22 nF, 10K, 470K, 2K2, 220 pF, 18K + 1M, 10K, 39K + 100 nF, 150K, 220 pF, 1 uF, 33K, 22K, 47 nF, 10K, 100K), which is the confirmation
  for everything except the clipper. **Its clipper drawing differs**, and a commenter on that page says so ("all the other references I have
  found online have the clipping section wired differently"); the wiring below is Aion's, and matches that commenter's description
  (M1: source to Vref, drain and gate to the signal; M2: drain and gate to Vref, source to the diode). AIND's simulated clipping levels
  (+5 V / -3.5 V absolute) were therefore not used.

## Topology (v1.7)
1. **Stage 1** (TL082, non-inverting): 22 nF -> 10K -> (+) (470K to the 4.5 V bias). (-) leg 2K2 + 68 nF to ground; feedback 220 pF and
   **18K + the Drive rheostat (1M audio)**: gain up to x460 above 0.5 kHz (176x at 1 kHz measured with Drive at the maximum, 81x at 200 Hz).
2. **10K into the clipper node**, 1 nF from it to the bias, and across it (all to the **bias, not ground**): two red LEDs antiparallel, and the
   two 2N7000s: **Q1** drain and gate on the node, source on the switch node; **Q2** drain and gate on the switch node, source on the node. The
   switch node goes to the bias through the **Clipping switch** (stock: connected; LED: open).
3. **10K into stage 2** (TL082, non-inverting, gain 1 + 150K/39K = x4.85; (-) leg 39K + 100 nF; 220 pF across the feedback), whose (+) input draws
   nothing.
4. **Output**: 1 uF, **33K** (and **22K** in parallel behind the **HP/LP switch**) into the treble bleed -- 47 nF to the **Tone** rheostat (10K
   linear) + 1K to ground -- and the Volume pot (100K linear).
   v1.7's DIP settings: no germanium diodes, no 10 nF hi-cut, 68 nF bass cap, 47 nF tone cap (C9 bypassed, C10 out).

## The 2N7000s: body diodes clip, the channels never turn on
A diode-connected NMOS is a two-terminal device: its **channel** conducts forward (drain to source) above the gate threshold (~2 V), its
**body diode** conducts the other way (source to drain) above ~0.65 V. Wired as above, Q1 and Q2 are antiparallel across the clipper node
and the bias: the node going up is held by Q2's body diode, going down by Q1's -- **at +-0.67 V from the bias** -- and a channel would need
2 V. So in stock mode the 2N7000s behave as two silicon diodes (with a slightly different knee), which matches Aion's statement that "the stock
clipping mode has a lower clipping threshold than the LED pair" (the red LEDs' 1.7 V). The channels are therefore not in the netlist (they
would cost two Newton ports, 6.8% instead of 5.3% of a core, with the output unchanged: +-0.673 V both ways; a test asserts the node stays
below the threshold). In the versions with germanium diodes (1.4, the Custom Shop) the Ge diode is in series with a body diode, not a
channel: Aion's warning that a reversed diode "blocks the path to ground and only half the waveform is clipped" is that polarity rule.
With the Clipping switch open (LED mode) the MOSFETs' common node floats between their body diodes and the LEDs clip at +-1.70 V.

## Model
* TL082: A0 2e5, 3 MHz gain-bandwidth, 100 ohm, swing 1.5-7.5 V. (The real part's output stops ~1.5 V short of each rail on 9 V: "the op-amps
  get into saturation" in AIND's words, which the macro's rails reproduce.)
* Red LEDs: the project's. 2N7000 body diode: Is 2 pA, N 1.4 (0.64 V at 0.1 mA, 0.73 V at 1 mA) -- **an assumption** (datasheet: V_SD < 1.5 V at
  0.4 A only); a factor 10 in Is moves the clip level by 83 mV.
* Pots: Drive 1MA (`pots::audio`), Tone and Volume linear ("B"). Tone: clockwise = more resistance in the shunt = brighter (Aion: "Tone fully
  up ... just a slight volume boost"). HP/LP: **High peak** = R10 connected in parallel with the 33K (13.2K), **Low peak** = 33K alone; from the
  schematic (the switch's other throw loops back to its own pole). Defaults: High peak, MOSFET clipping.
* The clipping and HP/LP switches are float parameters with two positions, shown by name ("MOSFET" / "LED", "Low peak" / "High peak"); the
  registry's other controls are the three knobs.

## Verification (`Tests/OCDStyleOverdriveProcessorTests.cpp`)
* DC: stage 1, the clipper node and stage 2 at 4.500 V.
* **Stage 1 gain against the closed form** (input divider, TL082 pole, 2K2 + 68 nF leg, (18K + Drive) || 220 pF), Drive 0.1 / 0.5 / 1 x 200 Hz /
  1 kHz / 4 kHz: within 0.12 dB up to 1 kHz, -0.42 dB at 4 kHz (bandwidth limit at 1x). **Stage 2**: 4.739x against 4.752x.
* Clipper: +-0.673 V (MOSFET mode) and +-1.701 V (LED mode). Tone 5 kHz / 200 Hz: 0.386 (Tone 0) -> 2.146 (Tone 1). HP vs LP at noon Tone,
  Volume max: 2.05x (+6.2 dB) at 1 kHz.
* Bounded and converging under a hot sine at every Drive setting in both clipping modes; stress with random knobs *and* switches (no failed
  solves); stereo independence.
* Aliasing (MOSFET mode; non-harmonic / harmonic at 1x / 2x / 4x / 8x): -25.1 / -40.6 / -62.0 / -75.6 dB.

## Not modelled
* The other versions' DIP options (Ge diodes, the extra 10 nF hi-cut, 100 nF bass cap, 22 nF tone cap) and their pot values (v1.1-1.2: Drive
  500kA, Tone 25kA; Volume 100kA/500kB in v1.3-1.5): these would be model parameters if wanted; the Aion table lists them.
* The TL082's real output stage, slew rate (13 V/us) and the "18 V" mode. The v2 (2017) circuit, which is "in-depth" different (AIND).
