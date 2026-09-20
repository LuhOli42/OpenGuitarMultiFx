# BD-2-Style Overdrive

Display name **"BD-2-Style Overdrive"**. Runs on
[`NodalCircuit`](./NodalCircuitSolver.md) — transistor level.

## Cost simplification (2026-09-20)
The three emitter/source followers (Q3 input buffer, Q7 gyrator buffer, Q1 output buffer)
are ideal followers, the tail-pair JFETs with their drain on the rail (Q11, Q13) are
one-port devices, and the clipper diode pairs are single ports; the gain stages' own
transistors (Q9/Q10, Q12/Q14) are untouched. Timbre within 0.8 dB of the full model.

## Source
The factory board schematic ("BD-2 MT BOARD", hobby-hour.com and
experimentalistsanonymous.com), read region by region at 2-3x zoom; the
"Analog Is Not Dead" circuit analysis for the intent of each stage. **The
schematic's notation, decoded:** a resistor drawn with a small arrow on a
hooked line returns to the **+4 V** reference (checked against every case: the
input JFET's gate bias, the emitter followers' base bias, the JFET switches'
bias). Audio rails are taken as +8 V (the supply's capacitance-multiplier output).

## Topology
1. **Input**: R18 10K, C14 47 nF, R15 1 M to 4 V -> **Q3 (2SK184GR JFET) source
   follower**, R19 10K; then the effect-path coupling C15 -> switch (Q6, modelled
   as its 100 ohm "on" resistance) -> C18 into stage 1's gate (R17/R22/R23 to 4 V).
2. **Gain stage 1 — a discrete op-amp**: JFET pair Q10/Q11 (shared 4.7K tail R30;
   Q10 drain load R28 2.2K, Q11 drain straight to the rail) driving **PNP Q9
   (2SA1335)**, collector load R32 2.2K, Miller cap C21 47 pF. Feedback: Q9's
   collector -> Gain rheostat (VR1A 250KA, wiper tied to one end) -> R29 22K ->
   Q11's gate, with R31 1.5K + C22 0.15 uF to ground as the shunt leg and C23 47 pF
   across. Closed-loop gain = 1 + (R29 + rheostat)/Z(R31+C22).
3. **Fixed tone stack** (R38, C34, C35, R50, R51, C26, R37) and **two pairs of
   diodes in series each way** (D7+D8 clip positive, D9+D10 negative: +/-2 Vf).
4. **Gain stage 2**: same topology with its own values (R33, R36, R34+C24 1 uF,
   R27 33K, C25/C20 100 pF) and the second Gain rheostat (VR1B).
5. **R26 5.6K || C17 + C19** low-pass, then Tone (C100/C101 0.018 uF around VR2 10K)
   and Level (VR3 100KA).
6. **Peak filter**: IC1B (ideal) with R8 5.6K || C8 2.2 nF feedback; the (-) node's
   shunt leg is C9 0.056 uF into a **gyrator** (Q7 emitter follower with C16 0.056
   uF coupling, R10 470K bias, R20 10K, R21 1.2K bootstrapped from its emitter) — a
   simulated inductor giving the bass peak.
7. **Output**: C7 -> switch (Q4) -> C6 -> Q1 emitter follower -> C1 -> R1 1K.

## Assumptions (not printed on the schematic)
2SK184GR: Idss 4.5 mA, Vp -1.2 V, lambda 0.02; 2SA1335 beta 200; 2SC2459 beta 300;
1SS133 = 1N4148-class (Is 2.52 nA, N 1.752). The circuit is closed-loop biased, so
DC is forgiving: the model's stage-1 collector lands at **4.158 V**, matching a
hand solution (~4.18 V) that needs Q10/Q11 to carry unequal currents from a
small input offset.

## Verification (Tests/BD2StyleOverdriveProcessorTests.cpp)
- DC: both stages' collectors 4.158 V (the 4 V reference + a small offset).
- Stage-1 gain at 2 kHz: min Gain **13.9x (22.9 dB)** vs the formula's 14.8x.
  Max Gain **97.7x (39.8 dB)** vs the ideal 172x (44.7 dB): a *discrete* op-amp has
  finite loop gain (open loop ~ a few hundred), which the ideal formula ignores.
- Overall response at min Gain (mV in): -7 dB @30 Hz, +12 @100, +17 @130 (a
  bump near 130 Hz, as owners measure), +16 @200, +18 @400, +20 @1k, +20 @3k.
- 3rd-harmonic distortion vs Gain: 0.11% / 20% / 26% / 27%; clipping peaks
  slightly asymmetric (0.503 / -0.531): the discrete stages saturating produce
  the even harmonics the pedal is known for.
- Tone: 4.6x more 4 kHz, dark to bright. Cost 4.8 us/sample (23% of a core, mono).

## Not modelled
The bypass path and the JFET switches' off-state leakage; the +9 V/+8 V/+4 V power
section (rails treated as ideal); IC1B's input protection diodes (they conduct
only in op-amp saturation, which ideal op-amps never reach); op-amp rail clipping.
