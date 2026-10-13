# Algorithmic vibratos

Processor: `Source/Effects/Modulation/Vibrato/AlgorithmicVibratoProcessor.{h,cpp}`. A Hermite-interpolated delay read at a
moving position (6 ms centre, up to ±5 ms), wet only.

| Key | What it is |
|---|---|
| `TapeWarbleVibrato` | A worn transport: wow that wanders (a new random target each Rate cycle, glided) plus ~11 Hz flutter; Age darkens the tone and deepens the flutter. |
| `StereoVibrato` | Left and right swept in opposition (sine or triangle). |
| `RandomVibrato` | A smoothed random pitch drift (Smooth = glide time between targets) instead of a periodic LFO. |
