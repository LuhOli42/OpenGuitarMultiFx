# Distortion+-Style Distortion and DOD 250-Style Overdrive

Display names **"Distortion+-Style Distortion"** (the original is the MXR Distortion+) and **"DOD 250-Style Overdrive"**
(the DOD Overdrive Preamp 250); the brand is dropped as for the BD-2 and Centaur. Registry keys
`DistortionPlusStyleDistortion` and `DOD250StyleOverdrive`; **one class**, `OpAmpClipperDistortionProcessor`, two models
(like the Tube Screamer's three), because the two pedals are the same circuit with different values (their builders say
so: the 250 is "almost exactly the same as the MXR Distortion Plus"). Runs on [`NodalCircuit`](./NodalCircuitSolver.md),
one block per channel, 2x oversampling at the High quality tier (`Orders {0, 0, 1}`). Cost 1.5-2.1% of a core at the
default quality (2.8-3.0% at 1x in the processor's own test; the rail clamps cost two Newton ports).

The circuit is one uA741 op-amp and two diodes. Its character comes from two things that an ideal-op-amp model would not
have: the **741's 1 MHz gain-bandwidth product** (at a gain of ~200 the stage is only ~5 kHz wide before anything clips)
and the **diodes running at a fraction of a milliamp** through 10K.

## Sources
* **Distortion+**: the MXR Distortion+ schematic in the GitHub `guitar-effects-schematics` collection (a redrawn 741
  schematic, read by eye), ElectroSmash's analysis (via its mirror, `electrosmash.mas-effects.com/mxr-distortion-plus-analysis.html`;
  the main domain did not resolve from this machine) and Wampler DIY's, for the values and the quoted figures.
* **DOD 250**: General Guitar Gadgets' schematic (`ggg_dist_250_sc.pdf`, the 741 version), read by eye; its parts list was
  not needed (the values are on the drawing).

## Topology (Distortion+ values / DOD 250 values)
1. **Bias**: two equal resistors from the 9 V supply with a decoupling cap: 1M + 1 uF / 22K + 10 uF. (The 250's optional
   100 ohm supply resistor is not modelled.)
2. **Input**: C2 10 nF -> R1 10K -> (+) pin; the pin returned to the bias by 1M / 470K. The 1 nF from the *jack* to ground
   (Distortion+ schematic; ElectroSmash says the same) loads the pickup, not this circuit: with an ideal source in front
   of it it is a no-op, so it is not in the netlist. (An earlier version put it at the (+) pin, which added a 15.9 kHz
   pole that the pedal does not have; the schematic was read wrong.)
3. **Op-amp stage (non-inverting)**: feedback 1M / 1M || 25 pF; the (-) leg to ground is 47 nF + 4.7K + the Gain rheostat
   (1M / 500K, *reverse log*: the 250's drawing says so, and it is assumed for the Distortion+). Gain = 1 + Rf / Z(leg):
   1.995 (min) ... 213.8 (max) for the Distortion+, 2.9 ... ~180 for the 250 (its 25 pF costs it 3 dB at 4 kHz).
4. **Clipper**: coupling cap 1 uF / 4.7 uF (blocks the 4.5 V) -> 10K -> a diode pair to ground with 1 nF across it
   (fc 15.9 kHz with the 10K); **1N270 germanium** / **1N4148 silicon**.
5. **Output**: the Output/Level pot across the diodes (its top is the diode node; 10K / 100K), wiper = output, into an assumed
   1 M load.

## Model
* **The 741** is `NodalCircuit::addOpAmpMacro` (added for this pedal; the RAT, Guv'nor and Blues Breaker use it too): a
  finite-gain constraint row (A0 = 2e5), a 1M + C pole (GBW 1 MHz -> 5 Hz dominant pole), the integrator node clamped by a
  diode to each rail, a saturating follower (output limited to 1.5 ... 7.5 V) and 75 ohm output resistance.
  **Why the clamp**: without it the integrator winds up while the output sits on a rail (the error between the inputs is
  ~1 V times an open-loop gain of 200 000, into a 5 Hz pole) and unwinds only after the input has reversed: measured
  560 us of delay at 440 Hz, a quarter of a cycle, varying with the signal level. Found by a test written while adding the
  RAT; the first version of this pedal shipped in the same session without it. With the clamp: 23 us (one sample).
* **Germanium diodes** (Distortion+): N = 1.3 and **Is = 5 nA**, fitted, not the Centaur's 1N34A model (Is 200 nA, right for
  that pedal's ~10 mA but +-0.24 V here). At this pedal's ~0.3 mA a hard-driven clip reaches +-0.37 V = 0.73 Vpp, which is
  ElectroSmash's measured "700-800 mVpp". This is a fit to that one figure, not an independent verification; the knee
  sharpness (N) is not constrained by any source.
* **Silicon diodes** (250): the project's 1N4148 model (Is 2.52 nA, N 1.752); +-0.53 V at the top of the range.

## Verification (`Tests/OpAmpClipperDistortionProcessorTests.cpp`, both models)
* DC: op-amp output and (-) input at 4.5000 V, diode node at 0 V (the coupling cap blocks the bias).
* **Small-signal gain vs the closed form** (input filters, 741 pole, feedback R || C, the (-) leg, complex arithmetic),
  4 Gain settings x 100 Hz / 1 kHz / 4 kHz: within 0.1 dB except at 4 kHz near the maximum gain (-0.4 dB at 1x, the
  theta-method's warping of a 4.7 kHz corner at 48 kHz; smaller when oversampled).
  ElectroSmash's quoted *minimum* gain of 1.5 (3.5 dB) does not match its own formula (1 + 1M/(1M + 4.7K) = 1.995); the model
  follows the arithmetic.
* Clip level 0.367 V peak (Distortion+; published 0.35-0.40) and 0.53 V (250), symmetric to < 2%.
* The op-amp output stays inside 1.5 ... 7.5 V under a hard drive and does reach both rails; **the clipped output leaves the
  rail within a sample of the input crossing** (23 us).
* Output/Level monotonic and silent at 0; no failed solves at any Gain setting under a 0.5 V sine; stereo identical/independent.
* Unity level (`PedalUnityLevelTests`): registry trims +6.20 dB (Distortion+), +0.51 dB (250).

## Not modelled / known gaps
* **741 slew rate** (0.5 V/us) and the input pair's own limiting. Not audible at guitar levels; the RAT's LM308 (0.3 V/us) is
  the case where it starts to matter (`RatStyleDistortion.md`).
* 741 input offset (1 mV x 213 = 0.2 V of output DC, blocked by C4), input bias current, the second pole above ~2 MHz.
* Supply: an ideal 9 V (a battery sags).
* Pot tapers: reverse-log for the Distortion pot of the Distortion+ is an assumption (documented for the 250).
* The 1 nF at the jack (see above), the 8-10 pF optional feedback cap of the Distortion+, the 250's optional input resistor.
* Diode knee sharpness; real 1N270 units vary a lot.
