# FX90-Style Delay (DOD FX90, key `FX90StyleDelay`)

Processor: `Source/Effects/Delay/BbdDelayProcessor.{h,cpp}` (shared engine; this model is a `case` in `prepare()`). Delay time: the clock is an RC oscillator whose period is proportional to the Delay pot's resistance, so the delay is linear in that resistance (through the pot's taper) between the published minimum and maximum.

Source: DOD FX90 Rev A 12-2-85. MN3005 + MN3101, NE570 compander, TL022s; 27K Sallen-Keys (1n8, 8n2 to TR1, 470p in;
2n2, 6n8 to the follower, 470p, twice out). De-emphasis 18K || (3.3K + 33n) in the output mixer (pre-emphasis its mirror). Repeat, Mix,
Delay (1M) → 20–400 ms.
