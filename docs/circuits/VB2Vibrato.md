# VB-2-Style Vibrato (Boss VB-2, key `VB2StyleVibrato`)

Processor: `Source/Effects/Modulation/Vibrato/VibratoPedalProcessor.{h,cpp}` (one class; this model is a `case`).

Boss's 1982 vibrato: MN3207 + MN3102 with a **true-sine** LFO (most BBD pedals use a triangle; the VB-2's sine is why
its pitch bends rounded), wet only. Modelled on the shared BBD with the CE-2 family's 3rd-order Sallen-Keys and
pre/de-emphasis (the VB-2 is that generation of Boss's BBD designs). **Rise Time** ramps the depth in after the
effect is engaged (0.01–3 s), the pedal's "unlatch" feature. Calibrated (no schematic sourced): ~4.5 ms centre, up to
±66 % sweep, Rate ~0.3–12 Hz.
