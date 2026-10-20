# Phase 45-Style Phaser (MXR, key `Phase45StylePhaser`)

Processor: `Source/Effects/Modulation/Phaser/PhaserPedalProcessor.{h,cpp}` (one engine; this model is a `case` in `prepare()`), building blocks in `PhaserParts.h`: TPT first-order all-pass stages (exact while R moves every sample), `Jfet` (ohmic-region square law, with the Vds term that makes these pedals distort a little), `LdrCell` (lamp/LED lag + CdS asymmetric response).

Source: Justin Philpott's MXR Phase 45 / DOD Phasor 201 drawing (MXR values). Two stages, 47n (".05") with 10K ||
2N5952: one notch — the subtle one. LFO Speed 500K, ~0.08–5 Hz.
