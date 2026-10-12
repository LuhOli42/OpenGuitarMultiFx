# SCH-1 (Arion SCH-1 Stereo Chorus)

Processor: `Source/Effects/Modulation/Chorus/SCH1StyleChorusProcessor.{h,cpp}` — key `SCH1StyleChorus`.

## Sources
- Arion SCH-1 schematic (yuvadm/guitar-effects-schematics, `Chorus/Arion SCH-1.gif`).

## What is modelled
- Q1 input follower; IC1a pre-emphasis with the **CE-2's values** (47K, 10K + 6.8n, 47K ‖ 100p).
- Wet: C7 33n / R11 100K, the CE-2's Sallen-Key (10K, 3.3n, 8.2n to Q2's emitter, 470p), MN3207 (1024), output filter
  R19 10K / C16 4.7n, R21 10K + C17 8.2n to Q3's emitter, R22 10K / C18 470p (C15 22n / R20 1M coupling).
- **Tone** (dry path, IC1b): `analog::networkTransfer` of R29 1K → {C37 2.2n → pot H, R56 33K → pot L, C36 15n to ground},
  wiper → C39 10n → R60 1M, then IC1b's 1 + R58 51K / (R57 33K + C38 10n) treble lift. 33 pot positions precomputed,
  coefficients interpolated (no audio-thread allocation).
- **Q5 phase splitter** (R27 = R28 = 10K): emitter and collector feed the two IC2 mixers through 22n + 39K (a 185 Hz
  high-pass on the wet) — OUT A = dry + wet, OUT B = dry − wet. De-emphasis feedback (R34 10K + C22 6.8n) ‖ R36 47K ‖ C24 100p.
- **LFO** (IC3): Schmitt R42 1M / R45 470K, integrator R43 470K + C32 100n, Rate = VR1 100KB dividing the square against R44
  5.6K → ~**0.6–11 Hz**. Depth = VR2 100KB from the triangle to R51 10K, R46 390K + C30 22n into Q7.
- **Clock** (MN3102): C31 47p charged through R49 160K, reset by Q8, the restart voltage set by Q7 through D4; MN3102 ÷ 2.

## Assumptions
- The Q7 → D4 → Q8 restart network is not fully legible: it is taken to halve the LFO the way the CE-2's R36/R37 do (without
  it the clock would run off past the MN3207's range at full depth). Swing at full depth ~3.6–5.1 ms.
- Q6 / D2 (bypass switching) not modelled.
