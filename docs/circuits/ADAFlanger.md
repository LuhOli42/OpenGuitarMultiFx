# A/DA-Style Flanger (key `ADAStyleFlanger`)

Processor: `Source/Effects/Modulation/Flanger/BbdFlangerProcessor.{h,cpp}`. This is the shared BBD flanger engine;
this model's values are in its `prepare()` case.

## Sources

- **Charlie Barth's trace of the original A/DA Flanger**, "Rev 01 & 02", 8 Feb 2007, the SAD1024 version. Published
  by moosapotamus, with the FlangerClone MN3007 rev 5 build documentation. That documentation carries the factory
  calibration: clock 34.8 kHz to 1.3 MHz at the CD4047.
- **A/DA's published specifications:** the 40:1 sweep, 20 Hz–16 kHz, 0.5 MΩ input.

These are community traces, not a factory drawing, but they agree with each other and with the published specs. The
earlier version of this page said no trustworthy drawing was found, and its filters were calibrated from the specs.

## What the trace shows, and what is modelled

| Stage | Circuit | Model |
|---|---|---|
| Input | C1 .01 into R5 1M; IC1a non-inverting with R4 30k over R2 30k + C2 4.7µ ∥ R3 2k7 + C3 .01 | `input` = the coupling high-pass × that pre-emphasis: ×2 in the lows, rising to ~×13 above 6 kHz |
| Into the BBD | IC1b inverting (R9 + R10 = 47k in, R11 47k ∥ C8 100p) with the 1N914 diode-bridge peak limiter (C6 2.2µ) on its input; R14 1k / C10 .01 | `bbdIn` = 1k / 10n. The limiter stays the engine's compressor (`compressorOnly`, unity below ~0.3 V, 2:1 above). The bridge-and-capacitor law itself is not modelled. |
| BBD | SAD1024, 1024 stages, clock 34.8 kHz–1.3 MHz | 0.39–14.7 ms |
| After the BBD | IC2c (R19 39k + C13 .22 in, R26 43k ∥ C14 100p); IC1c with **SW1 Even / Odd**; IC1d Sallen-Key low-pass R39 22k / R40 68k / C18 1500p / C19 510p (~4.7 kHz) | `bbdOut` = that Sallen-Key; Harmonics = the loop's sign |
| Regeneration | **Enhance** P1 10k through C9 0.1 (~159 Hz high-pass) back into IC1b; T2 trim | The 159 Hz high-pass plus the loop's top-end limit; up to 0.97 loop gain |
| Mix | Wet (D) and dry (C) each through **68k** into IC2b, R43 27k ∥ R44 5k1 + C20 .01 — the de-emphasis | `dryMix` / `wetMix` = that stage. With IC1a's pre-emphasis, the dry path comes out flat. |
| Threshold | IC3a / IC3b detector, D5 / D6, Q1 JFET switching the delay | The engine's gate (flanging off below a level) |
| Sweep | IC4 triangle LFO (P5 500k Speed) → Range / Manual → IC3c → CD4007 → CD4047 VCO | Rate 0.04–10 Hz, roughly exponential delay sweep |

## Level

PedalUnityLevel reads 0.00 dB at a registry trim of −0.15 dB. The drawn input and mixer gains set a slightly lower
through-level than the old flat chain, which used −3.31 dB.
