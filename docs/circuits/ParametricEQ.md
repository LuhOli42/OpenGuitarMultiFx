# Parametric EQ

Four-band parametric equaliser: low shelf, two bells (frequency, gain, Q), high shelf, plus output Level. Not a copy of one unit: it is the
classic console / rack layout, built from the RBJ "Audio EQ Cookbook" biquads in `Source/Effects/Biquad.h` (bilinear transform, so the gain
at the stated frequency is exact at any sample rate). Display name "Parametric EQ" (menu: Filter/FX).

* Page 1: Low (Gain, Freq), Low-Mid (Gain, Freq, Q), Level. Page 2: High-Mid (Gain, Freq, Q), High (Gain, Freq).
* Each band's coefficients follow its smoothed knobs every 16 samples: sweeping a knob does not zipper.
* Test: `Tests/ParametricEQProcessorTests.cpp` (gain at the centre frequency, shelves, flat at defaults).

There is no schematic behind it on purpose: an equaliser's transfer function is what defines it. Analog-modelled equalisers with component
values are the GE-7 (`GE7StyleEqualizer.md`).
