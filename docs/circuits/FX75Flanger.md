# FX75-Style Flanger (DOD FX75, key `FX75StyleFlanger`)

Processor: `Source/Effects/Modulation/Flanger/BbdFlangerProcessor.{h,cpp}` (shared BBD flanger engine; this model's values are in its `prepare()` case).

Source: DOD FX75 Flanger, redrawn by Fabian P. Hartery (`Flangers/DOD FX75.pdf`).

TL022 throughout, MN3007 + MN3101. Before the BBD: 10K / 1n and 47K / 120p, then **4.7K into an anti-parallel 1N4148
pair to ground** (`diodes::ShuntClipper`) — the FX75's grit when Regen is high. Output: 3 × 33K Sallen-Key (1n, 5n to U2A,
120p). Mixer U2B with 47K || (10K + 6.3n) || 120p de-emphasis (the matching pre-emphasis assumed in U1A). Regen 500K through
33K + 22n (a 219 Hz high-pass). Calibrated: MN3101 law → 0.8–10 ms; LFO 0.1–10 Hz.
