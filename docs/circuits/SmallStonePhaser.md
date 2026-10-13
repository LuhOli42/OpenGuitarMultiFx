# Small Stone-Style Phaser (EHX, key `SmallStoneStylePhaser`)

Processor: `Source/Effects/Modulation/Phaser/PhaserPedalProcessor.{h,cpp}` (one engine; this model is a `case` in `prepare()`), building blocks in `PhaserParts.h`: TPT first-order all-pass stages (exact while R moves every sample), `Jfet` (ohmic-region square law, with the Vds term that makes these pedals distort a little), `LdrCell` (lamp/LED lag + CdS asymmetric response).

Source: EHX Small Stone Issue J. Four **CA3094 OTA** stages with 6.8n: an OTA's transconductance follows its bias
current, which the LFO sets through a transistor — close to exponential in the LFO voltage — so the corner sweeps
exponentially (~90 Hz – 4 kHz here, calibrated). **Color** switches in the feedback path from the output back to the
input (resonant, vocal). Rate ~0.05–9 Hz.
