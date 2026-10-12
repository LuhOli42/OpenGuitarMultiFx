# A/DA-Style Flanger (key `ADAStyleFlanger`)

Processor: `Source/Effects/Modulation/Flanger/BbdFlangerProcessor.{h,cpp}` (shared BBD flanger engine; this model's values are in its `prepare()` case).

**No schematic was found that could be trusted** (DIY threads cite one, not reachable). Built from the A/DA's published
specifications and documented behaviour instead — every number here is calibrated, none derived from parts:
- delay **0.4–15 ms** (the "40:1" sweep; measured 0.4–15 ms on a real unit, Fractal Audio forum), rate **0.04–10 Hz**,
  20 Hz–16 kHz response (3rd-order filters scaled to ~16 kHz), 0.5 MΩ input;
- a **roughly exponential** LFO (dwells at the long end, snaps through the short one);
- a compressor ahead of the BBD (`Ne570::compressDown`: unity below ~0.3 V, 2:1 above -- no upward gain, so a quiet
  guitar is not pushed up) and a **band-pass in the feedback loop** (~160 Hz–5 kHz);
- **Enhance** up to 0.97 loop gain (self-oscillation territory, as the A/DA is known for), **Harmonics Even/Odd** = the
  feedback's sign, **Threshold** = a gate that turns the flanging off below a level.
Replace with a parts-level model if a reliable drawing turns up (Notion card).
