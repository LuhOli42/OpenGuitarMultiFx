# VB-2-Style Vibrato (Boss VB-2, key `VB2StyleVibrato`)

Processor: `Source/Effects/Modulation/Vibrato/VibratoPedalProcessor.{h,cpp}` (one class; this model is a `case`).

Boss's 1982 vibrato: MN3207 + MN3102 with a **true-sine** LFO (most BBD pedals use a triangle; the VB-2's sine is why
its pitch bends rounded), wet only. **Rise Time** ramps the depth in after the effect is engaged (0.01–3 s), the
pedal's "unlatch" feature.

## Source

No Boss schematic is reachable. The model uses the **BYOC Analog Vibrato** schematic (rev 2.0, 2018), a faithful
VB-2 clone. The original BYOC VB-2 drawing it replaced is gone, and the rev 2.0 file was recovered from the Wayback
Machine. Its audio path, which the model now uses:

- **Pre-emphasis:** IC1B with R5 47k ∥ (C3 6n8 + R4 7k5) in, and R6 47k ∥ C4 33p as feedback.
- **Into the BBD:** a Q2 Sallen-Key (R8 / R9 10k, C5 .01 feedback, C6 47p to ground), then the **MN3207**
  (1024 stages).
- **Out of the BBD:**
  - a Q3 third-order Sallen-Key (R13–R15 10k; C9 2n7, C10 4n7 feedback, C11 220p);
  - the output op-amp's de-emphasis: R20 47k in, R21 47k ∥ (C15 6n8 + R22 10k) ∥ C14 33p as feedback.
- **Modulation:** the MN3102 clock is current-controlled through a BA6110 OTA from a sine LFO (Rate C250k, Depth
  B50k).

## Still calibrated

The clock range depends on the DELAY trimmer, so the delay timing stays calibrated: ~4.5 ms centre, up to ±66 %
sweep, Rate ~0.3–12 Hz. These values used to borrow the CE-2's filters and emphasis (3n3 / 8n2 / 470p, 10k + 6n8 /
100p).
