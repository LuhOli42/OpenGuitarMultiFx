# BF-2-Style Flanger (key `BF2StyleFlanger`)

Processor: `Source/Effects/Modulation/Flanger/BbdFlangerProcessor.{h,cpp}` (shared BBD flanger engine; this model's values are in its `prepare()` case).

Source: Boss BF-2 schematic (`Flangers/BOSS BF-2.gif`).

The CE-2's front end (IC1a pre-emphasis 47K, 10K + 6.8n, 47K || 100p; matching de-emphasis in IC1b: R30 10K + C23 6.8n,
R31 47K, C22 100p). IC2a sums dry and Res with **R11 4.7K + D3/D4** across its 47K feedback (the limiter). A 2nd-order
Sallen-Key (10K/10K, C8 12n to Q2's emitter, C9 150p) feeds the MN3207 (1024); the output filter is 3rd order (10K × 3,
3.9n, 8.2n to Q3, 330p). Res = VR4 50KC / VR5 22KB, loop up to ~0.85. LFO (TL022): Rate VR3 with 1.5K into 2 × 33 µ,
Schmitt 100K/220K → ~**0.03–4.6 Hz**.

Calibrated: MN3102 clock law → 1–13 ms.
