# G-Series-Style Bus Compressor

Source: SSL "500 Series G Comp" user guide (2020), which describes the SL 4000 G bus compressor: VCA always in circuit, a "classic dominant"
side-chain (each channel rectified independently by a true-peak full-wave detector, the louder wins and controls both), attack 0.1 / 0.3 / 1 / 3 / 10 /
30 ms; ratio 2 / 4 / 10:1; release 0.1 / 0.3 / 0.6 / 1.2 s or Auto; a switched side-chain high-pass; "the knee point... purposely changes depending on the
setting of the RATIO control".

**Estimates** (the guide does not give them): 6 dB soft knee; threshold shift 0 / -3 / -6 dB for 10 / 4 / 2:1; the Auto release is a 0.1 s and a 1.2 s
release in parallel, the slower one weighted 0.75, its attack 4x slower; side-chain HPF corners off / 60 / 90 Hz. Controls: Threshold, Ratio, Attack,
Release, Make-up, SC HPF. Tests: `Tests/StudioCompressorTests.cpp` (static curve with the shifted threshold; attack switch).
