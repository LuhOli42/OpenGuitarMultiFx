# Tube Driver-Style Overdrive

Display name **"Tube Driver-Style Overdrive"**, registry key `TubeDriverStyleOverdrive`, class `TubeDriverStyleOverdriveProcessor`.
Runs on [`NodalCircuit`](./NodalCircuitSolver.md), two blocks per channel: two op-amp stages (one an ideal follower, one an
`addOpAmpMacro`) in block A; two Koren 12AX7 triodes and the passive tone network in block B. (The pedal is the Chandler Tube
Driver; the "-Style" convention applies, see `PositiveGroundBooster.md`.)

## Source
General Guitar Gadgets' redrawing of the Tube Driver by JD Sleep, **revision 2003-12-09** (schematicheaven.net
`ggg_chandler_tube_driver.pdf`). This is a legible redrawing, not the maker's drawing; it gives no part number for the op-amp, no
supply voltage and no tube voltages, so the operating point below is computed, not read.

## What the circuit is
1. **Input**: R1 10K, C1 0.033 uF, R2 1M to ground and C2 47 pF into IC1a, a unity-gain voltage follower (D1/D2 1N4148 from its input to the
   rails are protection: they cannot conduct for a guitar signal, so they are omitted, and IC1a is an ideal follower).
2. **IC1b, the "Tube Drive" stage**: C4 5 uF and R4 1K5 into the inverting input, (+) at ground, feedback = **R5 500K log used as a
   rheostat** || C5 120 pF. Gain = R5 / 1K5, from 0 to **333 (50 dB)** by the ideal formula. The op-amp's output limit is the supply
   rails, ~1.5 V short of each: this is where the pedal clips for real.
3. **The tube stage.** C6 0.01 uF and R6 10K into triode 1's grid, R7 10K from the grid to **V-**. Both triodes of the 12AX7 have
   their **cathode on the negative rail and their grid returned to it**: zero bias on a supply of only ~28 V, so plates are "starved"
   (68K plate loads R8/R11). Triode 1's plate goes through C7 0.047 uF and R9 10K to triode 2's grid (R10 10K to V-), and triode 2's
   plate drives the tone network. Positive grid swings draw grid current through R6 (a soft, tube-like ceiling); negative ones cut
   the tube off (the plate goes to V+).
4. **Tone**: P2 -> C8 330 pF -> the **Hi pot (R13 500K)**; P2 -> R12 22K -> N1, C10 0.047 uF from N1 to N2, C11 0.047 uF and R16 2K2 from N2 to
   ground, C9 0.1 uF from N1 to M (M = the bottom of the Hi pot), the **Lo pot (R15 100K, wired as a rheostat)** from M to N2, R14 220K from the
   Hi wiper to M, and the **Level pot (R17 100K log)** from the Hi wiper to ground; the Level wiper is the output. The network passes
   no DC from the plate (C8, C9, C10 block it).

## The supply (a computed operating point)
Half-wave rectified 12.6 V AC (17.1 V peak after a diode) into 470 uF each way; R18 10 ohm, R19 470 ohm to V+, R20 1K to V-, R3 1K (with
22 uF) feeding the op-amps' positive pin. With a NJM4558's quiescent current (3.5 mA) and the tubes' (~0.65 mA), the rails come out at
**V+ = +15.2 V, V- = -13.0 V**, and the op-amps see +11.7 / -13.0. The 12.6 V is a rating; the transformer's regulation can move each rail by
a volt or two, and that shifts the tubes' plate voltage. **Assumption.**

## Model
* IC1b: `addOpAmpMacro`, 1e5 gain, 3 MHz GBW, 75 ohm output, swing set 1.5 V inside its rails.
* Both triodes: the project's Koren 12AX7 (`TubeModels.h`), with Dempwolf grid current. **Revalidation at starved plate voltage**: the model was fitted at
  150-450 V for the Bassman. At Vgk = 0 it gives Ip 0.075 mA at 10 V, 0.198 mA at 20 V, 0.715 mA at 50 V, 1.89 mA at 100 V, 6.8 mA at 250 V (logged by the tests),
  a smooth monotonic curve with no dead zone (the plate-voltage floor in `floorPlateVoltage` is what keeps it so). I did **not** have
  published 12AX7 curves for the 10-30 V region to compare against, so **the tubes' plate operating point (7.1 V at rest, 20 V above the
  cathodes, 0.12 mA) is the model's, not a measured one.** The grids rest 0.08 V below V- (the grid current through the 10K leak).
* Pots: Tube Drive 500K audio taper, clockwise = more gain; Hi 500K linear, clockwise = brighter; Lo 100K linear, **clockwise = more
  resistance = more low end** (measured on the network: with 1 ohm the bass drops 20 dB at 100 Hz); Level 100K audio. Directions and tapers
  are assumptions except where measured.
* Output loaded by 1M. The bypass switch is not modelled.

## Verification (`Tests/TubeDriverStyleOverdriveProcessorTests.cpp`)
* Rails and DC point (above); grids at the cathode.
* IC1b's small-signal gain against the closed form with the finite GBW, the input network and C4/R4: 10.60x vs 10.60x, 49.16x vs 49.28x (0.02 dB), 154.3x vs 155.0x
  at Drive 0.2 / 0.5 / 0.8.
* Tube Drive and Level raise the output monotonically; **Hi 1 vs 0: +17.6 dB vs -10.7 dB at 6 kHz (rel. 1 kHz); Lo 1 vs 0: -5.3 vs -25.1 dB at 100 Hz.**
* Hot input and random knob moves: finite, solver failure rate 0, 2.37 iterations/sample; steady state clean (-421 dB).

## Oversampling and cost
Alias (`PEDAL_ALIAS=1`, a hard-driven probe, IC1b clipping against its rails): 1x -8.3 dB, 2x -20.8, 4x -33.8, 8x -49.4. **This is the worst of the
pedals so far**: the op-amp's rails give it the hardest edges. Orders {0, 1, 2} like the HM-2 (4x would be ~4x the cost); a hard-driven
pedal here still carries audible aliasing at the balanced tier. Cost ~6.2% of a core at 1x (above the 5% target, like the HM-2 / BD-2), ~12% at 2x.
Unity trim +4.01 dB.
