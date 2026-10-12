# Electric Mistress-Style Flanger (key `ElectricMistressStyleFlanger`)

Processor: `Source/Effects/Modulation/Flanger/BbdFlangerProcessor.{h,cpp}` (shared BBD flanger engine; this model's values are in its `prepare()` case).

Source: EHX "Standard Electric Mistress Model 3 — June 15 1981 — 1 battery" redraw (`Flangers/Electro Harmonix Electric Mistress 3.jpg`).

What makes the Mistress sound like itself is what it does NOT have: its only anti-alias pole is the 1n across IC2's 5K6
(~28 kHz), and after the SAD1024A there is just 470R into 10K || 3n3 (~107 kHz) — so the BBD's sampling images and aliasing
reach the output (the "glisten"). Modelled as such: those two poles ARE the BBD model's filters.
- Input 130K / 39n, buffer, IC2 inverting with 470R + (4K7 || 47n) in, 5K6 out (treble lift above ~720 Hz).
- **Passive mix**: dry through 22n + 13K, wet through 82n + 8K2, into 510R + 47n to ground (exact transfer).
- **Color** (feedback): wet → trim → 470R + 47n → pot → **2n2** into IC2's summing node — a differentiator: the regeneration
  is almost all treble. Its real transfer is used (not normalised).
- LFO (LM324): Schmitt 30K/10K, Rate 1M + 3K9 into 2 × 33 µ in series → **0.005–1.3 Hz** (the famous slow sweeps).
- **Filter Matrix**: the LFO is disconnected and Range sets the comb by hand.

**Band limit added:** on the pedal the clock images this leaves are ultrasonic (the SAD1024A runs at 32–640 kHz), but in a
48 kHz model they would fold back as audible aliasing the pedal never had (measured: −18 dB of energy above 8 kHz on a
guitar DI). Two ~17 kHz poles after the BBD stand in for the audio band's edge; what remains (~−30 dB above 8 kHz) is
the circuit's own +21 dB treble pre-emphasis — the Mistress's brightness.

Calibrated: the 311 + 2N5087 VCO → 0.4–8 ms. Range scales the whole LFO into the VCO (as the drawing's 100K divider does).
