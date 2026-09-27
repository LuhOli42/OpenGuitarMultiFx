# Metal Zone-Style Distortion

Display name **"Metal Zone-Style Distortion"**, registry key `MetalZoneStyleDistortion`, class `MetalZoneStyleDistortionProcessor`.
Runs on [`NodalCircuit`](./NodalCircuitSolver.md), **five blocks per channel** (a block holds at most one saturating op-amp), four
`addOpAmpMacro` stages that can clip, one linear op-amp, six real BJTs, one JFET and two diodes. (The pedal is the Boss MT-2 Metal Zone; the
"-Style" convention applies, see `PositiveGroundBooster.md`.)

## Source
The circuit diagram of the **service manual, MT-2, April 1991** (github apachiww/guitar_stompbox_collection
`Roland_Boss/Boss-MT-2-Metal-Zone-Schematic.pdf`), read at ~900 dpi. A manufacturer drawing: every value used is printed on it. All op-amps are
1/2 M5218AL, the JFETs 2SK184GR / 2SK118Y, the transistors 2SC3378GR, the diodes 1SS133. 9 V supply, a 4.5 V reference (the drawing's up-arrows).

## What the circuit is (signal order)
1. **Input**: R059 10K, C042 0.047 uF into Q011 (a JFET follower, gate 1M to the reference, R060 10K), C039 1 uF, **Q009 as the electronic-bypass
   series switch (modelled as 200 ohm, on)**, C033 0.015 uF, R043 100K into IC3b's (+).
2. **IC3b, first gain stage (block A)**: non-inverting, feedback R044 220K || C032 100 pF, and the (-) leg is **C034 0.027 uF into a bootstrapped
   emitter-follower branch** (Q010: C035 0.01 uF to its base, R046 2K2 to its emitter, R053 47K, R054 10K). Below ~340 Hz the base is an AC ground
   so R046 grounds the leg and the gain climbs to 1 + 220K/2K2; above it the base follows the signal, R046 sees no voltage, and the gain falls
   again: a **band-pass gain hump**, measured 34 dB at 800 Hz, -1 dB at 50 Hz and 7 dB at 12 kHz.
3. **IC3a, Dist (block B)**: R045 10K into (R042 10K || C031 0.047 uF) then C029 0.033 uF and R040 100K: a low-pass near 1 kHz. Non-inverting; (-) leg R041 1K +
   C030 10 uF to ground; feedback **C028 47 pF || (Dist pot 250KA as a rheostat + R051 1K)**. Gain 2 to 252.
4. **The clipper (block C)**: C027 10 uF, R033 2K2, **two 1SS133 back to back to ground** (symmetric ~±0.55 V), R032 10K, R031 4K7 + C023 0.015 uF, C021 1 uF, R029 100K into
   **IC4b** (non-inverting, feedback R030 3K3 || C022 47 pF, and **two bootstrapped-follower branches on its (-) leg**: Q008 with C024 0.015 uF / R034 1K / C025
   0.0015 uF and Q007 with C020 0.22 uF / R027 470 / C017 0.047 uF). Then **IC4a**, a unity-gain inverter (R028/R026 22K, C018 10 pF).
5. **Tone, first half (block D)**: R014 22K into node 7, IC1a non-inverting with R015 22K || C010 10 pF from its output to node 8. **High (VR03b 100K)** and **Low (VR03a 100K)** both go
   between node 7 and node 8 with their wipers to shunts: High's through R061 2K2 + C044 0.01 uF to ground; Low's into C008 0.22 uF and a bootstrapped high-pass (IC1b a
   follower, R012 2K2, C009 0.047 uF, R013 100K).
6. **Tone, second half and output (block E)**: C011 1 uF, then IC2a inverting with R038 47K / R035 47K || C026 100 pF, and its (+) input Z (R039 1M) fed by C038 0.1 uF from node 10.
   **Middle (VR02b 100K)** sits between R050 330 (from the input side) and R049 330 (from IC2a's output); its wiper goes into a follower (IC2b), C036 0.022 uF and R048 2K2. **Mid Freq
   (VR02a, 50K x2)** is two rheostats: node 10 to that R048 node, and node 10 to R062 2K2 to ground, with C043 0.0082 uF to ground on node 10. Then C005 10 uF, **Level (VR04 50KA)**, C004 1 uF,
   Q002 (the effect-side switch, 200 ohm), C002 10 uF, Q001 an emitter follower (R002 10K), C001 10 uF, R001 1K to the jack.

## Model
* Op-amps: `addOpAmpMacro`, 1e5 gain, 2.5 MHz, 75 ohm out, swing 1.5-7.5 V. IC4a a linear op-amp (its input is IC4b's rail-limited output, so it cannot clip in turn) sharing IC4b's block.
  IC1b and IC2b are ideal followers. A block cut between op-amps is a source plus the macro's 75 ohm (`NodalCircuit` restriction).
* Q010/Q008/Q007: real Ebers-Moll, `Is 1e-14, betaF 300` (the 2SC3378GR's gain is not on the drawing). Q011: `addJfet`, Idss 4.5 mA, Vp -1.2 V.
  Q009/Q002 are 200 ohm switches (the bypass electronics, the flip-flop and its diodes, are not modelled); Q001 an ideal follower with 0.65 V drop.
* The 1SS133s use the project's 1N4148 model.
* Pots: Dist 250K audio, clockwise = more; Level 50K audio; **High, Low and Middle 100K "G" taper assumed linear**; clockwise = boost (chosen by measurement: the wiper end that raises
  each band); **Mid Freq is a 50K "C" taper (`pots::law(x, 0.85)`), clockwise = LESS resistance = higher frequency**: measured on the network more resistance moves the boost lower, and
  the real knob's direction is an assumption.

## Verification (`Tests/MetalZoneStyleDistortionProcessorTests.cpp`)
* DC point: every op-amp output at 4.500 V, the jack at 0 V.
* IC3a's gain vs the closed form (finite GBW, input divider **including the C029/R040 branch that loads it**): within 0.15 dB at Dist 0.2 / 0.5 / 0.8, 400 Hz and 1.6 kHz (my first formula
  left the C029/R040 load out and failed by 0.36 dB; that was the test, not the model).
* IC3b's gain hump (above). Dist raises the gain, Level the output, monotonically.
* Small-signal ranges: **High +23 dB at 6.4 kHz (1 vs 0), Low +38 dB at 100 Hz, Middle +23 dB at 400 Hz (1 vs 0)**; with Middle at 1, the boost sits around 400-800 Hz at Mid Freq 0.75 and 1.6-3.2 kHz at
  Mid Freq 1.0. These ranges are what the netlist gives with the pots' ends wired as drawn; I had no reference measurement of a real MT-2 to compare them against, and the pot
  taper ("G") is an assumption, so treat the exact dB figures as unverified.
* Hot input (3 V) and random knob moves: finite, solver failure rate 0, 5.6 Newton iterations/sample over the five blocks. Steady state clean (-178 dB).

## Oversampling and cost (the honest part)
**~15% of a core at 1x, before any oversampling** (~30% at 2x): far above the 5% target, and each block costs at least one Newton iteration (~2000 cycles), so five blocks is a floor for this
netlist. Alias (non-harmonic/harmonic, `PEDAL_ALIAS=1`, a hard-driven probe): 1x -21.9 dB, 2x -27.8, 4x -39.9, 8x -75.8: **the worst of the pedals so far, because IC3b, IC3a, the diodes and IC4b all clip in
turn**. Orders {0, 1, 2} like the HM-2. Unity trim +2.23 dB. A cheaper model (fewer blocks, a reduced clipper) is possible but would have to be checked by ear against this one first; none is built.
