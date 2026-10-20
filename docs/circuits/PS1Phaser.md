# PS-1-Style Phaser (Maestro / Oberheim, key `PS1StylePhaser`)

Processor: `Source/Effects/Modulation/Phaser/PhaserPedalProcessor.{h,cpp}` (one engine; this model is a `case` in `prepare()`), building blocks in `PhaserParts.h`: TPT first-order all-pass stages (exact while R moves every sample), `Jfet` (ohmic-region square law, with the Vds term that makes these pedals distort a little), `LdrCell` (lamp/LED lag + CdS asymmetric response).

Source: Oberheim PS-1 Phase Shifter schematic. The first phaser pedal (1971): **six** 1458 all-pass stages (100K / 100K,
0.01 µF, a 2N4302 with 100K to the bias) — three notches. No speed knob: three fixed speeds on the footswitches (Slow /
Medium / Fast; 0.15 / 0.75 / 5.5 Hz here, calibrated). Low input impedance (0.33 µF into 33K).
