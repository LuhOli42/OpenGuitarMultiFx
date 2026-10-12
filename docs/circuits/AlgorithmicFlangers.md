# Algorithmic flangers

Processor: `Source/Effects/Modulation/Flanger/AlgorithmicFlangerProcessor.{h,cpp}` — four keys, one class. Hermite-
interpolated delay lines (`EnsembleDelay`) with feedback.

| Key | What it is |
|---|---|
| `ThroughZeroFlanger` | **Tape flanging**: a reference "tape" delayed a fixed 4 ms and a moving one swept 0–8 ms, mixed equally. When the moving tape passes the reference the comb's notches run off to infinity — the "through zero" swoosh of the original studio technique (two tape machines, a thumb on a reel), which no BBD pedal can do since a BBD can't reach zero delay. Mix 0 = reference only. |
| `BarberpoleFlanger` | An endless sweep: four delay ramps a quarter-cycle apart, each faded in and out with sin² (they sum to a constant), so the comb rises (or falls, Direction) forever — a Shepard-tone illusion for flanging. |
| `InstantFlanger` | After the Eventide Instant Flanger (FL201): Manual sets the delay, an LFO (Sweep, Rate) and the input envelope (Envelope) move it, 0.25–12 ms, wet lightly band-limited (the original was analog). |
| `EnvelopeFlanger` | No LFO: the pick's envelope moves the delay (Sensitivity, Range, Decay, Direction). |
