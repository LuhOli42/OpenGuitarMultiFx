# CE-1 (Boss CE-1 Chorus Ensemble)

Processor: `Source/Effects/Modulation/Chorus/CE1StyleChorusProcessor.{h,cpp}` — key `CE1StyleChorus`.

## Sources
- Boss CE-1 service schematic, board ET-10C (yuvadm/guitar-effects-schematics, `Chorus/Boss CE-1.gif`), including its
  oscilloscope annotations (waves A–N).
- ElectroSmash's CE-2 article for the CE-1's context (50K input impedance, mono/stereo outputs).

## What is modelled
- **Preamp — the CE-1's character**: Level (VR11 50KA) into IC-1 (TA7136P), non-inverting, gain 1 + R11/R12 = 1 + 470K/10K
  = **48** above ~34 Hz (C07 .47 µ), on ±13 V rails → clipped at ±12 V with a 1 V knee. The service notes confirm the gain:
  80 mVpp at B → 4 Vpp at C.
- **Wet path**: a passive treble emphasis (R13 22K ‖ C06 6.8n into R5 10K) feeding a Sallen-Key (R14/R16 10K, C12 4.7n to Q2's
  emitter, C11 1n) — one ladder with a zero; C14 .1 µ / R21 100K; MN3002 (512 stages); 4th-order reconstruction (R41 22K / C28
  4.7n, R42 6.8K + C26 1n and R43 15K + C29 1n to Q10's emitter, R44 15K / C27 470p).
- **Mix**: from the service notes, referred to IC-1's output at 4 Vpp: M (mono) 240 mVpp of dry, N 350 mVpp of wet at 440 Hz;
  the wet gain divides the filters' own 440 Hz gain back out. Chorus = dry + wet; Vibrato = wet only.
- **Chorus LFO** (IC-4, µPC1458): Schmitt R80 180K / R81 220K (the 23 Vpp triangle of wave K), integrator C43 82n via R76 18K;
  Chorus Intensity = VR12 between R82 100K, R65 1.5K and R57 100K. Range ~**0.4–3 Hz**.

## Calibrated, not derived (see why)
- **Clock law**: the multivibrator (Q3–Q6) and its control network are not legible enough on the scan. Taken from the notes'
  own waveforms instead: wave G, clock period 5–16 µs; waves E/N, 3 ms delay at chorus minimum. Chorus: 85.3 kHz ±25 %
  (2.4–4 ms). Vibrato: 131 kHz ± up to 69 kHz (the full 5–16 µs) × Depth.
- **Vibrato LFO**: wave L shows a sine at Vibrato; rate 0.5–8 Hz on the reverse-log VR14 is an estimate.
- High-sensitivity (mic) input and the stereo M = dry / N = wet split are not modelled: the output is the mono mix.
