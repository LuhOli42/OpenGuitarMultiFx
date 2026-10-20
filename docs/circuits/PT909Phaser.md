# PT-909-Style Phaser (Ibanez, key `PT909StylePhaser`)

Processor: `Source/Effects/Modulation/Phaser/PhaserPedalProcessor.{h,cpp}` (one engine; this model is a `case` in `prepare()`), building blocks in `PhaserParts.h`: TPT first-order all-pass stages (exact while R moves every sample), `Jfet` (ohmic-region square law, with the Vds term that makes these pedals distort a little), `LdrCell` (lamp/LED lag + CdS asymmetric response).

Source: Ibanez PT-909 (MP-PO1001A) schematic. Four 1458 stages with **2SK30A** JFETs (selected) across 30K, 0.047 µF;
**Feedback** (10KB) from the last stage back to the first, **Width** (100KB) the LFO depth, Speed (500KC). 2SK30A taken
as Idss 3 mA / Vp −1.5 V.
