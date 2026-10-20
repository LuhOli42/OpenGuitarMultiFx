# AD9-Style Delay (Ibanez AD9, key `AD9StyleDelay`)

Processor: `Source/Effects/Delay/BbdDelayProcessor.{h,cpp}` (shared engine; this model is a `case` in `prepare()`). Delay time: the clock is an RC oscillator whose period is proportional to the Delay pot's resistance, so the delay is linear in that resistance (through the pot's taper) between the published minimum and maximum.

Source: Dirk Hendrik's redraw "Ibanez AD-9 analog delay" (2007). MN3205 (4096) + MN3102, NE571 compander (U2.A / U2.B),
pre/de-emphasis 47K, 10K + 6n8, 47K || 100p. Anti-alias R11/R13/R14 10K, C8 10n, C10 82n (to T2), C11 330p;
reconstruction 5th order (10K, 2n2, 39n, 1n → T3; 10K, 39n, 820p → T4). Delay Time 10KC (reverse taper) → **10–300 ms**,
Repeat (up to 1.04), Delay Level.
