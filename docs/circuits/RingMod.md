# Ring Mod

The guitar multiplied by a carrier through a **diode bridge**, not an ideal multiplier (Maestro RM-1, Moog MF-102, the Dalek voice), so it
has the gritty sidebands and a carrier that is cancelled but not perfectly gone.

Model: Julian Parker, "A simple digital model of the diode-based ring-modulator" (DAFx-11): `out = D (c + x/2) - D (c - x/2)`, with D the
piecewise diode shaper of his eq. (2): bias vb = 0.2 V, knee vL = 0.4 V, slope h = 1 (the paper's reference implementation). Here the
output is scaled by `1/gain` after the difference (`out = (D (c + xs) - D (c - xs)) / gain`, xs = drive-scaled input). A first version had an extra
x2 that made the sidebands twice as big; removed.

Controls: Frequency (carrier 0.5 Hz .. 5 kHz), Drive (x0.5 .. x8 into the diodes), Shape (sine / triangle / rounded square), Mix.

The diode shaper creates harmonics falling 20 dB/octave, so the registry runs it oversampled (`Orders {1, 2, 3}`). Test:
`Tests/RingModProcessorTests.cpp` (sideband frequencies f_c +- f_in, carrier leakage far below the sidebands).
