# Echo Flanger (EHX EH-7700-Style, key `EchoFlangerStyleFlanger`)

Processor: `Source/Effects/Modulation/Flanger/BbdFlangerProcessor.{h,cpp}` (shared BBD flanger engine; this model's values are in its `prepare()` case).

Source: EHX "Echoflanger EH-7700" signal path and oscillator sheets, Rev #157, 8 Mar 1978 (`Flangers/Electro Harmonix Echo Flanger 1/2.jpg`).

- **NE570 compander** around the delay (compressor IC2A before, expander IC2B after).
- Two SAD1024A halves in series (1024 stages); **Slapback** switches IC4 in (2048 stages) and adds C37 150p to the 311 VCO's
  47p, slowing the clock ~4.2× → ×8.4 delay (~4–85 ms). Chorus mode moves the window to 4–20 ms; Filter Matrix freezes the LFO.
- 3.01K Sallen-Key filters both sides (values partly hand-annotated on the scan — 3n, 47n, 2n7 / 2n2, 18n, 4n5).
- Rate (2M log) → ~0.13–14 Hz; Width, Tune (manual), Feedback (10K log), Blend switch (Effect only / Blend).
