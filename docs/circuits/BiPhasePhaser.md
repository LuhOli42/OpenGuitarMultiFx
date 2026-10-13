# Bi-Phase-Style Phaser (Mu-Tron, key `BiPhaseStylePhaser`)

Processor: `Source/Effects/Modulation/Phaser/PhaserPedalProcessor.{h,cpp}` (one engine; this model is a `case` in `prepare()`), building blocks in `PhaserParts.h`: TPT first-order all-pass stages (exact while R moves every sample), `Jfet` (ohmic-region square law, with the Vds term that makes these pedals distort a little), `LdrCell` (lamp/LED lag + CdS asymmetric response).

Two independent **six-stage opto phasers** (LED-lit CdS cells: fast light, the cell's own lag), each with its sweep
generator — sine or **square** (the Bi-Phase's signature hard-switching sweep) — with Rate, Depth, Feedback switch.
Mode: Stereo (phaser A left, B right, their sweeps a quarter cycle apart) or Series (A into B: twelve stages). Behavioural:
the drawing available is a DIY re-creation, not Mu-Tron's own; stage capacitors and the cells' range are calibrated.
