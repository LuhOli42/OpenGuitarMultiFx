# Crunch Box-Style Distortion

Display name **"Crunch Box-Style Distortion"** (after the MI Audio Crunch Box). Registry key `CrunchBoxStyleDistortion`, class
`CrunchBoxStyleDistortionProcessor`. Runs on [`NodalCircuit`](./NodalCircuitSolver.md), **two blocks per channel** (each holds one op-amp
macro), cut at the first op-amp's output like the [Blues Breaker](./BluesBreakerStyleOverdrive.md). 1x cost 4.3% of a core; tiers
`{0, 1, 2}` (2x / 4x at Normal / High). Unity trim -14.25 dB.

## Source
* **"MI audio CRUNCH BOX DISTORTION", drawn by matsumin, 2006-09-17** (Schematics Online: `Mi-Audio-Crunch-Box.pdf`): a clean CAD drawing
  with every value. This is the circuit modelled, read at 2x.
* **Aion FX "Crescent"** documentation (`crescent_legacy_documentation.pdf`, page 3): "inspired by the MI Audio Crunch Box ... most similar
  to a version 3". Its first two stages match the matsumin drawing value for value (22 nF, 1K, 1M, 1 nF; 1K + 220 nF leg; 100K Drive pot as
  feedback + series; 100 pF; 100 nF, 10K, 1M || 100 pF), which is the confirmation for the topology; its output section is a later version
  (below).

The Crunch Box has had four versions since 2006 (the last is the "Super Crunch Box"); this is the earliest drawing found. I did not find a
schematic labelled by version, so **which version the user's ears know is not established**.

## Topology
1. **Stage 1** (LM833, non-inverting): 22 nF -> 1K -> (+) (1M to the 4.5 V bias, 1 nF to ground). (-) leg: **1K + 0.22 uF to ground**
   (the gain rises above ~720 Hz: this is the "Marshall" mid focus). Feedback: the **Drive pot** (100K-B, linear) between (-) and its
   wiper, **the wiper being the op-amp's output**, with 100 pF across that part. The rest of the pot is a series resistance into stage 2 --
   the Blues Breaker's arrangement, so turning Drive up raises stage 1's gain (x3 .. x100) and lowers the resistance in front of stage 2.
2. **Stage 2** (LM833, inverting): 0.1 uF, 10K, (-); (+) at the bias; feedback **1M || 100 pF** (gain -1M/(10K + the pot's series part):
   x9 .. x100, with a 1.6 kHz pole at the top).
3. **Output**: 2.2 uF, 1K, **two red LEDs to ground** (the only clipper), 100 ohm, the **Tone** rheostat (10K "C", into 39 nF to ground),
   10K, a **4K7 + 22 nF** shelf to ground, the Volume pot (100K-B).

Total gain reaches ~x6700 (76 dB) with Drive at the maximum: the pedal is a very hard clipper whose character is the LEDs (1.8 V) behind a
narrow mid-gain window.

## Model
* LM833: A0 3.16e5, 15 MHz gain-bandwidth, 37 ohm out, swing 1.5-7.5 V (macro-model, [NodalCircuit](./NodalCircuitSolver.md)).
* Red LEDs: the project's (Is 1.3e-19, N 1.9: 1.75 V at 0.5 mA), as in the Guv'nor and the Nobels pedals.
* Pots: Drive and Volume linear (B), Tone "C" (reverse-log, `pots::reverseAudio`) wired as a rheostat so that **clockwise = less resistance
  = brighter** (an assumption: the drawing does not say which end is the wiper's start).
* Bias 4.5 V, the supply filter and the reverse-polarity diode ideal. The output node carries a 1M load.

## Verification (`Tests/CrunchBoxStyleDistortionProcessorTests.cpp`)
* DC: both op-amp outputs at 4.500 V, the LED node at 0 V.
* **Stage 1 gain against the closed form** (input divider, LM833 pole, 1K + 0.22 uF leg, feedback || 100 pF), Drive 0.1 / 0.5 / 1.0 x 200 Hz /
  1 kHz / 4 kHz: within 0.22 dB (25.7x at 200 Hz, 77.8x at 1 kHz, 89.6x at 4 kHz with Drive at maximum).
* **Stage 2 gain** (measured as stage-2-out / stage-1-out): 8.91x / 16.29x / 86.37x at Drive 0 / 0.5 / 1, against 8.92 / 16.30 / 86.55 from
  the formula (within 0.02 dB).
* LEDs clip at +-1.808 V, symmetric to 3%. Tone: 5 kHz / 200 Hz is 0.039 (Tone 0) -> 0.221 (Tone 1). Volume monotonic.
* Bounded and converging at every Drive setting; stress test (random knobs, plucked notes, hot bursts) with no failed solves; stereo
  independence.
* Aliasing (non-harmonic / harmonic, 1x / 2x / 4x / 8x): -23.3 / -38.7 / -57.6 / -67.9 dB.

## Assumptions and what this model does not say
* **Version**: the early (2006) output section. The Aion drawing (a version-3-like circuit) has, after the same two stages: 2.2 uF, **470 ohm**
  (not 1K), the red LEDs **plus four silicon diodes (two in series each way)** to ground, a 10K "C" Tone into **22 nF** (not 39 nF), a
  25K **Presence** pot with 22 nF, Volume 100K-B; and a TC1044 charge pump for 18 V operation. Those later diodes clip at ~1.2 V, before the
  LEDs would, which changes the character; they are not in this model.
* The Tone taper/direction, the LM833 (vs the RC4558 or TL072 of other versions) and the bias supply are as stated above. The pedal's LED
  indicator and switching are not part of the audio path.
* The LM833's slew rate (7 V/us) is not modelled; at these swings (a few volts at kHz) it does not limit.
