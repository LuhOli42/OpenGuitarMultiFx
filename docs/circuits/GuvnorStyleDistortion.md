# Guv'nor-Style Distortion

Display name **"Guv'nor-Style Distortion"** (the original is the Marshall The Guv'nor). Registry key
`GuvnorStyleDistortion`, class `GuvnorStyleDistortionProcessor`. Runs on [`NodalCircuit`](./NodalCircuitSolver.md): two
blocks per channel (cut at the first op-amp's output), each with one `addOpAmpMacro` TL072. 2x oversampling at the High tier
(`Orders {0, 0, 1}`). Cost 3.4% of a core at the default quality.

## Source
ElectroSmash's Marshall Guv'nor analysis (`electrosmash.mas-effects.com/marshall-guvnor-analysis.html`): its full schematic
(`marshall-guvnor-schematic-parts.png`, read region by region at zoom) and its tone-control drawing
(`marshall-guvnor-tonestack.png`), which is the only source with the tone stack's values. The text of that page has three
values that disagree with its own drawing (C13 "47 nF" against 470 pF on the drawing, where 22K x 470 pF gives the quoted
15.3 kHz; and the stage gains, below); the drawing was followed.

## Topology
1. **Gain stage** (TL072 A, non-inverting): C1 9.6 nF -> (+) with R2 1M to the 4.5 V bias (R13/R14 47K + C14 10 uF);
   (-) leg R3 2.2K + C3 100 nF to ground (unity at DC, bass roll-off 723 Hz); feedback = **the Gain pot (100K) from (-) to
   its wiper, which is the op-amp output**, with C2 120 pF across it. Gain = 1 + R(pot, (-) side)/Z(leg): 1 ... 46.
2. **The Gain pot's other half** (wiper to its far end) is a *series resistance* into the second stage, through C4 220 nF and
   R4 10K (and C5 100 nF) into the inverting stage's (-). ElectroSmash's text treats the second stage as a fixed -68x (R5/R4);
   on the drawing the pot's far end feeds it, so its gain is 680K / (10K + the rest of the pot): **6.2x at minimum Gain, 68x at
   maximum**. Total 15.8 dB ... 70 dB. (The Blues Breaker has the same arrangement.)
3. **Clipping stage** (TL072 B, inverting): R5 680K || C6 220 pF feedback, (+) at the bias; C7 220 nF -> R6 1K -> **two red LEDs
   back to ground** (the tone stack's input node), clip point 1.8-2 V.
4. **Tone stack** (a Marshall/Big-Muff-style passive network, highly interactive): R7 1.5K, C9 4.7 nF, C8 100 nF, R8 680, R9 680,
   C10 220 nF, R10 100 + C11 10 nF, C12 68 nF and three 10K pots (Bass, Middle, Treble); traced node by node from the drawing
   (`GuvnorStyleDistortionProcessor::buildChannel` has each element commented with its designator).
5. **Level** 100K (audio taper), R11 22K, C13 470 pF, into an assumed 1 M load.
* The status LED's leg on the coupling-cap island is replaced by a 100M resistor (a DC path only). The stereo loop jack and the
  bypass switching are not modelled.

## Model
* TL072: A0 2e5, GBW 3 MHz, 100 ohm output, swing 1.5-7.5 V (`addOpAmpMacro`, with its windup clamp).
* Red LEDs: N = 1.9, Is = 1.3e-19 A (1.75 V at 0.5 mA, 1.9 V at 3 mA); fitted to the quoted 1.8-2 V, not a datasheet.
* The direction each tone pot turns is not on the schematic; it is the direction in which the knob raises its own band
  (Bass at 100 Hz, Middle at 1 kHz, Treble at 5 kHz), which the tests check. Pot tapers (linear tone pots, linear Gain) are assumptions.

## Verification (`Tests/GuvnorStyleDistortionProcessorTests.cpp`)
* DC: both op-amp outputs at 4.5000 V, LED node at 0 V.
* Stage 1 gain vs the closed form (input HP, TL072 pole, R3/C3 leg, feedback || C2), 3 Gain settings x 200 Hz / 1 kHz / 4 kHz:
  within 0.25 dB (1.00 / 19.2 / 37.6x at 1 kHz).
* Stage 2 gain (with the pot's series segment) vs the closed form: within 0.1 dB (4.5 / 8.2 / 47x).
* LED clip: 1.797 V peak (published 1.8-2), symmetric to 3%.
* Each tone knob raises its own band; Level monotonic; no failed solves at any Gain setting under a hot sine; stereo.
* Unity level: registry trim +0.06 dB.
* **Not verified against the real pedal**: the tone-stack response (no numbers published; ElectroSmash gives graphs only,
  "a subtle mid-frequency scoop between 1 and 3 kHz").

## The "dying" bug (2026-09-21)
The user reported the Guv'nor going silent for no reason. Cause: the solver, not the circuit -- see "Coupled junctions" in
`NodalCircuitSolver.md` (two clamp diodes of the op-amp macro, a failed solve freezes the circuit and burns 300 iterations per
sample). Reproduced with `Tests/PedalStress.h` (random knob jumps, plucked notes, x6 bursts): 31-94% failed solves; fixed: 0.
The same test now runs on the RAT, Blues Breaker, Distortion+ and DOD 250.

## Not modelled
TL072 slew (13 V/us; irrelevant here), the stereo effects loop, the status LED, input R1 2.2M (anti-pop; ideal source), pot
tapers (assumed), the supply's sag.
