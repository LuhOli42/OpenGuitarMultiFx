# SAD-1-Style Delay (Arion SAD-1 stereo delay, key `SAD1StyleDelay`)

Processor: `Source/Effects/Delay/BbdDelayProcessor.{h,cpp}` (shared engine; this model is a `case` in `prepare()`). Delay time: the clock is an RC oscillator whose period is proportional to the Delay pot's resistance, so the delay is linear in that resistance (through the pot's taper) between the published minimum and maximum.

Source: Arion "Stereo Delay SAD-1 / PCB SAD-1A". MN3205 + MN3102, uPC1571 compander, the CE-2-family emphasis
(10K + 6n8, 47K, 47K || 100p). Anti-alias 10K × 3 with 2n2, 47n, 330p; reconstruction 2n2, 33n, 1n. **Stereo**: OUT 1 = dry +
wet, OUT 2 = dry − wet (the second TA75558 mixer takes the wet inverted) — a stereo chain gets OUT 1 left, OUT 2 right.
Delay (500KB) 20–300 ms, Repeat (100KC), Depth (100KC, the wet level).
