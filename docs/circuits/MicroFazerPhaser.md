# Micro Fazer-Style Phaser (Univox, key `MicroFazerStylePhaser`)

Processor: `Source/Effects/Modulation/Phaser/PhaserPedalProcessor.{h,cpp}` (one engine; this model is a `case` in `prepare()`), building blocks in `PhaserParts.h`: TPT first-order all-pass stages (exact while R moves every sample), `Jfet` (ohmic-region square law, with the Vds term that makes these pedals distort a little), `LdrCell` (lamp/LED lag + CdS asymmetric response).

Source: Gottfried Divos's "Univox Micro Fazer" (2003, also Melos PS-110 / Ibanez PS-110). Four 1458 all-pass stages,
50n with 100K || a selected **2SK34D**; LFO: Rate 1M log + 27K into 4.7 µ, Schmitt 220K / 33K → **0.34–13 Hz**
(from parts). Mix 22K / 22K, equal.
