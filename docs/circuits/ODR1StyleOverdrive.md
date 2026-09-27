# ODR-1-Style Overdrive

Display name **"ODR-1-Style Overdrive"**, registry key `ODR1StyleOverdrive`, class `ODR1StyleOverdriveProcessor`. Runs on
[`NodalCircuit`](./NodalCircuitSolver.md), three blocks per channel, three 4558 stages as `addOpAmpMacro`, and one real
transistor. (ODR-1 is a Nobels Electronics model name; the "-Style" convention applies, see `PositiveGroundBooster.md`.)
Shares its input buffer and output stage with the DT-1 (`Source/Effects/NobelsCommon.h`).

## Source
Nobels Electronics' own circuit diagram, **ODR-1 rev. 5, 7 Dec 2000** (schematicheaven.net `nobels_odr1_overdrive.pdf`), read
at 300-500 dpi. A manufacturer drawing, not a reverse-engineering; every value used is printed on it.

## What the circuit is
1. **Input**: the shared JFET buffer (R1 33K, Q1, C1 0.22 uF, R4 330K, the bypass switch Q5, R5 330K), then C10 0.1 uF and R10
   2K7 into stage 1's (+), with R11 10K and C11 22 nF to ground: a high-pass near 160 Hz and a low-pass near 2.7 kHz.
2. **Stage 1 (U1A), the drive**: non-inverting, (-) leg = (820 + 82 nF) || (1K5 + 2.2 uF), so the gain rises in two steps.
   Feedback = two **4148 back to back** and 120 pF straight across it, plus the **Drive pot (250KA)** from (-) to the
   output with its wiper tied back to (-) through R14 1K8. Net feedback resistance: from ~1.8K (wiper at the output end) to 250K.
   Gain from ~1.2 at the bottom of the knob to **~470 (53 dB)** at the top by the ideal formula (feedback 250K over the two
   legs in parallel, ~530 ohm); measured, with the input network in front, stage 1 gives 97x at 300 Hz and 141x at 1 kHz at full Drive
   (the 120 pF across the feedback and the input band-pass take the rest). The diodes limit the output to about ±0.42 V (measured).
3. **Shunt clipper and filter**: C20 2.2 uF, R20 12K into C21 2.7 nF and two 4148 back to back to ground (a second clipper, like
   a DS-1's), then R21 39K, 82 nF into (1 nF || 12K to ground), and R23 10K into stage 2.
4. **Stage 2 (U2A) and the Spectrum control.** U2A's (+) has R24 43K to ground and (R25 5K1 || C24 22 nF) to the Spectrum pot's pin
   6; its (-) has R27 10K to ground, a 20K || 560 pF feedback and R26 1K2 to the pot's pin 4. The 30KB pot's **wiper** carries a
   network to ground: 27 nF, and through 0.1 uF the **Q2 network** — 8.2 nF to the base of a transistor (150K to ground), 2.2K to
   its emitter (3.3K to the battery negative). That is a bootstrapped emitter follower, and seen from the wiper it behaves like
   an inductor whose finite value comes from the transistor's base current. Moving the wiper decides how much of that resonant
   shunt sits on U2A's inverting side (where it raises the treble gain) against its non-inverting side (where it loads the
   signal), so **Spectrum sweeps a resonant shape** across the mids and treble instead of tilting.
5. **Stage 3 (U2B)**: R32 4K7 and C31 8.2 nF (4.1 kHz low-pass) into a non-inverting stage: leg R35 1K2 + 1 uF, feedback R33 22K
   || 4.7 nF || (R34 5K1 + 82 nF): gain ~19 in the mids, more above ~380 Hz through the series branch.
6. **Level (50KA)** after C30 2.2 uF, then the shared output stage: 1 uF, 330K, a series switch (Q7), 330K, 1 uF into a 4558
   voltage follower (R43 150K), C43 3.3 uF and R45 150K to the jack.

## Model
* 4558: 1e5 gain, 3 MHz GBW, 75 ohm out, swing 2.0-7.0 V on a 9 V supply (`addOpAmpMacro` with its windup clamp).
* 1N4148: the project's standard (Is 2.52 nA, N 1.752).
* **Q2 is a real Ebers-Moll transistor** (collector on the 9 V rail, `betaF 250`, an assumption: the C2362G's gain is not
  given). An ideal follower would give the base an infinite input impedance and change the "inductor" the Spectrum network
  depends on. Q1 (JFET), Q5/Q7 (series switches, 200 ohm on) and the output follower are ideal, as in the DT-1.
* Three blocks: stage 1 with the input; the shunt clipper, filter, Spectrum network and U2A; then U2B, Level and output. Each op-amp
  output drives the next block through its 75 ohm output resistance (an op-amp output cannot be loaded back). The Drive pot's two
  segments live in the first block (both are in U1A's feedback loop), the Spectrum pot's in the second.
* Pots: Drive 250K with the 15% audio law, Spectrum 30K linear, Level 50K with the 15% audio law.

**Assumptions you cannot see on the drawing:** which lug of each pot is clockwise. Drive clockwise = more; Spectrum clockwise =
the wiper toward pin 4 = brighter (checked by the model: 3 kHz / 400 Hz goes from 0.49 to 1.05 across the knob, so the direction
is at least consistent with what the wiring does); Level clockwise = louder.

## Verification (`Tests/ODR1StyleOverdriveProcessorTests.cpp`)
* DC: all three op-amp outputs at **4.500 V**, all three blocks converged (Q2 included).
* **Stage 1's small-signal gain against the closed form** (input divider and network, the two-shelf leg, the Drive pot's feedback
  including its 1K8, the 120 pF, the diodes' small-signal conductance, and a finite-GBW op-amp), 3 Drive settings x 300 / 1k / 2.5k
  Hz: within **0.03 dB at 300 Hz** and 0.08 dB at 1 kHz; the worst cell is 0.42 dB at 2.5 kHz with a gain of 160, the
  trapezoidal integration's frequency warping at 48 kHz (the RAT doc records the same effect). The diodes' zero-bias
  conductance is 2.7% of the gain at full Drive; without it in the formula the test failed by 0.08 dB, which is what found it.
* Stage 1 clips symmetrically at **+0.422 / -0.422 V** on the two 4148.
* Drive 0 -> 1 raises the output of a 5 mV signal from 0.025 to 1.10 V p-p.
* Spectrum changes the response by 13 dB at 150 Hz, 20 dB at 3 kHz and 27 dB at 6 kHz between its ends (small signal, logged in the
  test), and is brighter clockwise.
* Level near-silent at 0 (0.00004 V p-p) and monotonic; hot bursts and random knobs: finite, **0.000000** solver failure rate, 3.49
  Newton iterations/sample over the three blocks.
* Steady-state clean: non-periodic error **-180.8 dB** against the -80 dB bar.

## Cost, oversampling, unity
* **6.83% of a core at 1x** (dev PC, dual-mono): three blocks, three op-amp macros, a BJT and four diode ports.
* Aliasing (non-harmonic/harmonic, `Oversampling.md`): **-32.3 dB at 1x, -52.1 dB at 2x, -64.8 dB at 4x**, -90.6 dB at 8x. Worse
  at 1x than the Tube Screamer (-41 dB; two 4148 in feedback plus a shunt clipper both clip), so tiers **1x / 2x / 4x**.
* Unity trim **-1.77 dB** (the pedal is 1.8 dB louder than the reference at noon).

## Not modelled
The electronic bypass and its JFET/logic switching; the real gain (~0.9) and distortion of the JFET followers; the status LED;
battery and supply filtering (the 9 V rail is ideal); Q2's real gain (assumed 250).
