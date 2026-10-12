# AD80-Style Delay (Ibanez AD80, key `AD80StyleDelay`)

Processor: `Source/Effects/Delay/BbdDelayProcessor.{h,cpp}` (shared engine; this model is a `case` in `prepare()`). Delay time: the clock is an RC oscillator whose period is proportional to the Delay pot's resistance, so the delay is linear in that resistance (through the pot's taper) between the published minimum and maximum.

Source: Ibanez AD80 (MP-AD1001A) schematic. MN3005 + MN3101, NE570 compander, TL022s. Anti-alias 10K × 3 with 6n / 6n / 3n3;
reconstruction two sections (3n3, 3n3, 1n8; 2n2, 2n2, 1n2). Output de-emphasis 10K || (1K + 15n) (pre-emphasis taken as its
mirror). Controls as on the pedal: Repeat (10KC), Blend (50KB, dry ↔ delay), Delay Time (500KA, audio taper) → 15–330 ms.
