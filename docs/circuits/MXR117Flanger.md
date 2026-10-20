# MXR M117-Style Flanger (key `MXR117StyleFlanger`)

Processor: `Source/Effects/Modulation/Flanger/BbdFlangerProcessor.{h,cpp}` (shared BBD flanger engine; this model's values are in its `prepare()` case).

Source: "MXR Flanger Original" drawing by Stellan Lehrberg (yuvadm/guitar-effects-schematics, `Flangers/MXR Flanger.gif`).

From the drawing: +15 V single supply. NE5532 input stage 1 + 10K / (1K + 33n) — a +21 dB pre-emphasis 438 Hz → 4.8 kHz,
undone by the output mixer's 100K || (10K + 3.3n). The BBD summer (4558, 150K in, 100K feedback) has **27K + an anti-parallel
1N4148 pair** across its feedback: the limiter that lets Regen go to the edge without blowing up (`diodes::FeedbackLimiter`,
solved exactly). Regen = the wet back through the 50K lin pot, 15n + 120K (an 88 Hz high-pass) — max loop gain 100K/120K.
1K / 10n in front of the SAD1024, a 7.5K / 2.4K Sallen-Key (6.8n, 3.3n) after it. LFO: Schmitt (100K/82K) + integrator
(4.7K + 500K log Sweep, 2 × 15 µF in series) → **0.08–8.6 Hz**.

Calibrated: the BC557/BC548 + 4013 clock's law → delay 0.35–7 ms; SAD1024 taken as one 512-stage section; wet/dry set
equal at 1 kHz (the reconstruction stage's make-up gain is not legible).
