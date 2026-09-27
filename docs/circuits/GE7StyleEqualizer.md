# GE-7-Style Equalizer

Seven-band graphic EQ (100 / 200 / 400 / 800 Hz, 1.6 / 3.2 / 6.4 kHz) plus a Level slider, solved component by component on `NodalCircuit`
from the manufacturer's circuit diagram (hobby-hour.com). Display name "GE-7-Style Equalizer" (menu: Filter/FX). Source:
`Source/Effects/GE7StyleEqualizerProcessor.{h,cpp}`, test `Tests/GE7StyleEqualizerProcessorTests.cpp`.

## Circuit

* Input stage: non-inverting, with a treble pre-emphasis (x1 .. x11 above ~2.3 kHz).
* Level stage: non-inverting; the slider moves a 2.2K / 10 uF between the (+) input and the feedback node: +-15 dB.
* The equaliser: one op-amp whose (+) node and (-) node are each joined, through the band sliders, to an RLC branch to the bias. Every branch is a
  **gyrator**: an inductor simulated with a follower (0.06 .. 1.85 H) -- see below.
* De-emphasis (cancels the pre-emphasis) and an emitter-follower output buffer.
* Gain law of the equaliser: `H = (1 + R25 * sum(Gb_T)) / (1 + R24 * sum(Ga_T))`; with every slider centred the two sums are equal and the whole path is flat.

## Gyrator

The gyrator (Z = R17 + (sL' || R16), L' = C * R16 * R17) is solved as its **equivalent inductor** `L = Cgyr * Rgyr * Rloss` in parallel with `Rgyr`
and in series with `Rloss`, not as its own op-amp/follower net: it keeps each block below `NodalCircuit`'s 32 unknowns (the first version had
more than 32 and failed the DC solve). The result is four blocks per channel (input, level, equaliser, output network), cut where the next
stage draws almost nothing.

## Measured (tests)

* Flat within -0.65 dB with every slider centred (the registry adds +0.65 dB of trim so the pedal is unity, see `UnityLevel.md`).
* Each band's slider at +-full gives +-12 .. +-15 dB at its centre; the 6.4 kHz band only ~8 dB (the pre/de-emphasis and the branch's
  Q limit it; the test bound was relaxed for it and this is how the circuit is drawn, not a bug).
* Stereo: the two channels are separate circuits.

## Not modelled

Part tolerances, op-amp noise, the exact op-amp (a generic saturating macro).
