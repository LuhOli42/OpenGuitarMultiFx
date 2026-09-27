# DT-1-Style Distortion

Display name **"DT-1-Style Distortion"**, registry key `DT1StyleDistortion`, class `DT1StyleDistortionProcessor`. Runs on
[`NodalCircuit`](./NodalCircuitSolver.md), two blocks per channel, with the 4558 as `addOpAmpMacro`. (DT-1 is a Nobels
Electronics model name; the project's "-Style" convention applies, see `PositiveGroundBooster.md`.)

## Source
Nobels Electronics' own circuit diagram, **DT-1 rev. 4, 4 Feb 2000** (schematicheaven.net `nobels_dt1_dist.pdf`), read at
300 dpi. It is a manufacturer drawing, not a reverse-engineering, and every value used here is printed on it. The same
maker's ODR-1 (rev. 5) has the same input buffer, switching and output stage.

## What the circuit is
Two 4558 stages driven by **one** pot.
1. **Input**: R1 33K into the gate of Q1 (a BC264D JFET source follower; R2 1M to the battery negative). C1 0.22 uF, R4
   330K, then C10 33 nF into stage 1's (+) with R10 22K to ground: a **219 Hz high-pass** in front of the gain, so the bass
   is thin going into the clipping (a large part of why it does not go mushy).
2. **Stage 1 (non-inverting)**: (-) leg = 0.68 uF + 3K3 (unity at DC, gain from ~70 Hz up). Feedback = R12 10K + the
   Distortion pot's wiper-to-end segment, with **two red LEDs and 1.5 nF straight across it**. Gain 1 + (10K + R)/3K3:
   4 with the pot's segment at 0, ~80 with it at 250K; the LEDs limit the output to about ±1.55 V (measured), a soft clip.
   The 1.5 nF also rolls the treble off as the gain goes up (a pole at 408 Hz for the 260K case).
3. **The Distortion pot (250KB) has its wiper on stage 1's output.** One end goes through R12 into stage 1's (-)
   (feedback), the other end to the junction of C13 2.2 nF / R13 3K3 (the input of stage 2). So turning it up raises
   stage 1's gain (more feedback resistance) and lowers stage 2's series input resistance at the same time. C13 sits
   across the input-side segment, so the more resistance there is the more it is bypassed at high frequencies
   (treble-heavy at low Distortion).
4. **Stage 2 (inverting)**: R13 3K3 and C14 68 nF into its (-); feedback = 390K || 330 pF || **one 4148 one way || two 4148 in
   series the other**. Gain from ~1.5 (pot's series segment 250K) to ~118 (segment 0). The diodes clip asymmetrically:
   about -0.6 V one way and +1.2 V the other.
5. **Tone (50KB)**: a treble path (4.7 nF from stage 2's output) to one end of the pot and a bass path (15K into 68 nF to
   ground, then 150 nF and 68K to ground) to the other, the wiper going to Q2's gate. The middle is always partly
   scooped: it is a blend of a high-pass and a low-pass, not a tilt.
6. **Level (50KA)** after a 1 uF, then C24 / R34 330K, a series JFET switch (Q7), C40 1 uF into the base of Q4 (R40 200K), an
   emitter follower, C41 3.3 uF and R42 200K to the jack.
Total gain at the top of the Distortion knob is about 80 x 118 = **9400x (79 dB)**, split across two clippers.

## Model
* 4558: open-loop gain 1e5, GBW 3 MHz, 75 ohm out, swing 2.0-7.0 V on a 9 V supply (`addOpAmpMacro` with its windup clamp).
* 1N4148: the project's standard (Is 2.52 nA, N 1.752). Red LED: the Guv'nor's (Is 1.3e-19, N 1.9).
* Q1, Q2 and Q4 are ideal followers (Q1, Q2 JFET, Q4 BJT, each with its DC drop; a real JFET follower has gain ~0.9 with its
  3K3/10K source resistor, which is not modelled). The JFET series switches Q5 and Q7 (the electronic bypass) are 200 ohm
  resistors while on; the bypass logic, the true-bypass relay path (Q3, Q6, C30, C31) and the LED are not modelled.
* Two blocks: stage 1 with its input; then stage 2, Tone, Level and the output, fed by stage 1's output through its 75 ohm
  output resistance (an op-amp output cannot be loaded back). The Distortion pot's two segments live one in each block.
* Pots: Distortion 250K linear, Tone 50K linear, Level 50K with the project's 15% audio law.

**Assumptions you cannot see on the drawing:** which lug of each pot is clockwise. Distortion clockwise = more (the wiper
toward the feedback end), Tone clockwise = brighter. The wiring makes the *ends* unambiguous; the rotation direction is not
printed.

## Verification (`Tests/DT1StyleDistortionProcessorTests.cpp`)
* DC: both op-amp outputs at 4.500 V, both blocks converged.
* **Stage 1's small-signal gain against the closed form from the schematic** (input divider, C1/R4/Q5/R5/C10/R10 high-pass,
  then a non-inverting stage with a finite-GBW op-amp and the feedback in parallel with 1.5 nF), 3 Distortion settings x 300 /
  1k / 3k Hz: **within 0.22 dB everywhere** (worst at Distortion 0.5, 3 kHz; the rest under 0.16 dB).
* Stage 1 clips symmetrically at **+1.546 / -1.546 V** on the red LEDs.
* Stage 2 clips at **+1.010 / -0.505 V: a ratio of exactly 2.00**, one diode against two in series.
* The single pot raises the output of both stages together: 5 mV in gives 0.022 -> 0.452 V p-p from Distortion 0 to 1.
* Level near-silent at 0 (0.00001 V p-p) and monotonic; Tone 0 -> 1 takes the treble/bass ratio from 0.29 to 4.42.
* Hot bursts and random knobs: finite, **0.000000** solver failure rate, 2.59 Newton iterations/sample (both blocks).
* Steady-state clean: non-periodic error **-154.7 dB** against the -80 dB bar.

## Cost, oversampling, unity
* **6.17% of a core at 1x** (dev PC, dual-mono): two blocks, two op-amp macros, four diode ports. Above the 5% target, in the
  HM-2's bracket.
* Aliasing (non-harmonic/harmonic, `Oversampling.md`): **-29.4 dB at 1x, -47.3 dB at 2x, -64.7 dB at 4x**, -68.0 dB at 8x. The
  soft LED stage does most of the clipping work first, so each doubling buys ~18 dB; tiers 1x / 2x / 4x.
* Unity trim **+10.17 dB** (the pedal is 10 dB below the reference at noon: the 219 Hz high-pass and the two clippers take a lot
  of level).

## Not modelled
The electronic bypass and its JFET/logic switching; the real gain (~0.9) and distortion of the JFET followers; the status LED;
battery and supply filtering (the 9 V rail is ideal).
