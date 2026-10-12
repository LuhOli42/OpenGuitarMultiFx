# Tri-Chorus (algorithm, after the Dytronics Tri-Stereo Chorus)

Processor: `Source/Effects/Modulation/Chorus/TriChorusProcessor.{h,cpp}` — key `TriChorus`.

Not a circuit model: the Dytronics CS-5 Tri-Stereo Chorus (the Songbird/LA studio rack) is known for its three voices
modulated 120° apart, and that is what this reproduces. Three taps on one Hermite-interpolated delay line
(`EnsembleDelay`), each swept by a sine LFO offset by a third of a cycle; wet band-limited at 9 kHz with a 2-pole
low-pass (the analog original was BBD-based). Stereo: L = 2/3 voice 1 + 1/3 voice 2, R = 2/3 voice 3 + 1/3 voice 2; mono:
all three equally. Rate 0.1–4 Hz (exponential), Depth ±0–3 ms, Delay 4–20 ms, Mix. ~0.1 % of a core.
