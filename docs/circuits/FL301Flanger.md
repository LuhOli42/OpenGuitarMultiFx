# FL-301-Style Flanger (Ibanez FL-301, key `FL301StyleFlanger`)

Processor: `Source/Effects/Modulation/Flanger/BbdFlangerProcessor.{h,cpp}` (shared BBD flanger engine; this model's values are in its `prepare()` case).

Source: Ibanez FL-301 (MP-FL0401A) schematic (`Flangers/Ibanez FL301.pdf`).

MN3007 + MN3101, TL022s. Regeneration (10KB) summed into IC102a ahead of a 24K / 24K Sallen-Key (1n to Q101, 470p);
after the BBD 24K / 24K (2.2n to IC102b, 820p). IC101b mixes with 51K || (5.1K + 3n) || 51p de-emphasis (pre-emphasis
assumed to mirror it). Controls: Delay Time, Speed (500KC), Width (100KB), Regeneration. Calibrated: 1–13 ms, 0.1–10 Hz.
