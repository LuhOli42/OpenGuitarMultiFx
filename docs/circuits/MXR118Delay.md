# M118 Analog Delay-Style (MXR, key `MXR118StyleDelay`)

Processor: `Source/Effects/Delay/BbdDelayProcessor.{h,cpp}` (shared engine; this model is a `case` in `prepare()`). Delay time: the clock is an RC oscillator whose period is proportional to the Delay pot's resistance, so the delay is linear in that resistance (through the pot's taper) between the published minimum and maximum.

Source: MXR "Schematic diagram analog delay" 1977 (`MXR Analog Delay 1-3.png`). **Six SAD1024 sections in series (3072
stages)**, NE571 compander, a discrete VCO divided by a 4013 chain. Delay **30–300 ms**, Mix pot (dry ↔ delay), Regen.
The drawing's transmission-gate de-glitchers and filters are partly illegible; filters approximate (3rd order in, two
2nd-order out, ~8–12 kHz — which, at the long end where the clock drops to ~5 kHz, lets the BBD's aliasing through: the
M118's grit).
