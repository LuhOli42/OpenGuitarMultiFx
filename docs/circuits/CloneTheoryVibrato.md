# Clone Theory-Style Chorus/Vibrato (EHX, key `CloneTheoryStyleVibrato`)

Processor: `Source/Effects/Modulation/Vibrato/VibratoPedalProcessor.{h,cpp}` (one class; this model is a `case`).

Source: "clonetheory" redraw (Tomboy 2004, `Chorus/Electro Harmonix Clone Theory MN3007.jpg`). MN3007 clocked by a CD4047
whose timing is trimmed by a 2N4302 JFET driven by the LFO. IC1A gains the input ×3.9 (390K / 100K); R5 10K + C4 22n
ahead of the BBD (a dark ~720 Hz corner — the Clone Theory's murk); IC1B Sallen-Key (51K, 680p / 680p) after it.
**Chorus-Vibrato** blend pot (dry ↔ wet, both at its centre), Rate (1M log), Depth, **Vibraflange** switch (R21/R22 82K
feeding the output back — flanger-like resonance). Calibrated: ~6 ms centre, ±60 % triangle sweep.
