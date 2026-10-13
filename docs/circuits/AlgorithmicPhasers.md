# Algorithmic phasers

Processor: `Source/Effects/Modulation/Phaser/AlgorithmicPhaserProcessor.{h,cpp}`. Exponential sweep 80 Hz – 5 kHz;
Mix crossfades the input into the all-passed signal (0.5 = deepest notches, 1 = the phase-shifted signal alone).

| Key | What it is |
|---|---|
| `BarberpolePhaser` | Three 6-stage phasers ramping a third of a cycle apart across the whole sweep, each faded in and out with sin² (summing to one): the notches rise (or fall) forever. |
| `EnvelopePhaser` | 6 stages whose corner follows the pick (Sensitivity, 4 ms attack, 150 ms release, Direction). |
| `QuadraturePhaser` | 8 stages per side, the two LFOs 90° apart: wide stereo motion that collapses cleanly to mono. |
| `StepPhaser` | A sample-and-hold sweep: a new random position each LFO cycle, with Glide between steps. |
