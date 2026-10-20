# Phase 90-Style Phaser (MXR, key `Phase90StylePhaser`)

Processor: `Source/Effects/Modulation/Phaser/PhaserPedalProcessor.{h,cpp}` (one engine; this model is a `case` in `prepare()`), building blocks in `PhaserParts.h`: TPT first-order all-pass stages (exact while R moves every sample), `Jfet` (ohmic-region square law, with the Vds term that makes these pedals distort a little), `LdrCell` (lamp/LED lag + CdS asymmetric response).

Source: ElectroSmash, *MXR Phase 90 Analysis* (Script logo, BOM, notch maths). Four all-pass stages, each 47n with
**24K || a 2N5952** (Idss ~6 mA, Vp −2.5 V — the datasheet's −1.3..−3.5 V middle); the LFO (Schmitt + integrator, Speed
pot) swings Vgs from near 0 toward cut-off. With the FET off each stage turns at 141 Hz and the two notches sit at the
article's 58 / 340 Hz; the test locks the corner's sweep (a few hundred Hz .. a few kHz). Circuit switch: **Script** (no
R28) or **Block** (R28 24K feeding the output back into stage 2's input: the mid hump, the "crunchier" later units).
Output = dry + wet, equal (Q1's mixer). Input impedance 480K. Calibrated: the FET parameters and the LFO's bias/swing
(trimmer-set on the real pedal), speed 0.14–6.5 Hz.
