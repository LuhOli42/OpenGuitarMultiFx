# String Ensemble (algorithm, after the Solina / ARP String Ensemble)

Processor: `Source/Effects/Modulation/Chorus/StringEnsembleProcessor.{h,cpp}` — key `StringEnsemble`.

Not a circuit model. The Solina-family ensemble: three delay taps (~5 ms) each swept by the SUM of two sine LFOs — slow
~0.6 Hz (±1 ms) and fast ~6 Hz (±0.15 ms) — with both LFOs spread 120° across the taps. The fast LFO gives the shimmer,
the slow one the width; together they turn one voice into a section. Wet band-limited at 8 kHz. The original's output is
wet only; Mix is an equal-power crossfade from dry to ensemble. Speed scales both LFOs 0.5×–2×, Depth scales both sweeps
(nominal at noon).
