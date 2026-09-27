# dbx 160-Style Compressor

Studio unit, **feed-forward**, true-RMS detection, gain computer in dB, VCA that follows its control exactly. Source: dbx's published specs
(via the Waves dbx 160 guide): attack 15 ms for a 10 dB step, 5 ms for 20 dB, 3 ms for 30 dB; release 8 ms for 1 dB, 80 ms for 10 dB, 400 ms for 50 dB
(~125 dB/s constant).

**What makes it a 160**: the detector. A first-order averager of the signal's *power* with tau = 34 ms reproduces all of these (derived, not
guessed): attack 15 / 6.4 / 2.6 ms, release 7.7 ms / 78 ms / 390 ms (130 dB/s); the test's measured 10 dB release is 87 ms.
Gain law: Giannoulis-Massberg-Reiss quadratic soft knee (`CompressorCommon.h`), 10 dB wide for Over Easy (the width is not published), hard knee
when Over Easy is off. Ratio 1:1 .. infinity (>= 39.5 = infinity). Controls: Threshold, Ratio, Output, Over Easy.

Not modelled: the VCA's own distortion and noise. Test: `Tests/StudioCompressorTests.cpp` (4:1 static slope, release time).
