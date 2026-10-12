# CE-2 (Boss CE-2 Chorus)

Processor: `Source/Effects/Modulation/Chorus/CE2StyleChorusProcessor.{h,cpp}` — registry key `CE2StyleChorus`, browser chip **Chorus**.

## Sources
- Boss CE-2 schematic (yuvadm/guitar-effects-schematics, `Chorus/BOSS CE-2.pdf`; the common redrawn sheet).
- ElectroSmash, *Boss CE-2 Analysis* (via web.archive.org): BOM, pre/de-emphasis numbers, the Sallen-Key cutoffs, how the
  MN3101 VCO works (C22 charged through R38, Q5 discharging it, the LFO setting the restart voltage through Q4/D1).
- Holters & Parker, DAFx-18 — the BBD model (`BucketBrigadeDelay`).

## What is modelled, part by part
| Block | Parts | Model |
|---|---|---|
| Input buffer | C1 .047 into ~407K (R1 + R2 ‖ βR3), C2 .47 into R4 | two coupling high-passes (8 Hz, 7 Hz) |
| Pre-emphasis (IC1a) | R4 47K ‖ (R5 10K + C3 6.8n), R6 47K ‖ C4 100p | exact inverting transfer, bilinear; rail-clipped at ±3.5 V |
| Wet coupling | C5 .033 into R8 100K | 48 Hz high-pass |
| Anti-alias | R9–R11 10K, C6 3.3n, C7 8.2n to Q2's emitter, C8 470p | `analog::ladderTransfer` → poles 6.59 kHz (real) and 6.93 kHz (pair) — ElectroSmash's 6.6 / 6.9 kHz |
| BBD | MN3007, 1024 stages | `BucketBrigadeDelay` |
| Reconstruction | R16–R18, C11–C13 (same values) | same ladder, as the BBD's output filter |
| Wet couplings | C10 .033 / R15 330K; C14 .033 into R22 | 14.6 Hz; the C14/R22 corner (~100 Hz) is inside the mixer transfer |
| Mixer + de-emphasis (IC1b) | R21 = R22 = R24 = 47K, R23 10K + C15 6.8n, C16 100p | exact transfers for dry and wet; 50/50 mix, dry path flat |

Modulation (shared by both channels):
- **LFO** (IC2, TL022): Schmitt (R29 33K / R30 47K) + integrator (R32 1M, C19 0.1 µ). Rate = VR1 100KB dividing the
  ±3.5 V square against R31 10K; the wiper's own resistance is added to R32. Range **0.32–3.56 Hz** (ElectroSmash: 3.5 Hz max).
- **Depth** (VR2 100KB) divides the triangle about the 4.5 V bias, R35 220K + C21 10n smooth it into Q4 (whose input
  resistance, β(R36+R37), loads it).
- **Clock**: C22 (47p) charges through R38 (150K) from the restart voltage `(Ve(Q4) − Vbe)/2 − V_D1` toward 9 V; the
  MN3101 switches at ~VDD/2 and Q5 resets C22 (0.3 µs assumed). Clock = oscillator / 2.
  Full depth sweeps **3.2–5.3 ms**; depth at zero sits at ~4 ms.

## Not modelled / assumptions
- BBD noise, clock bleed and the chip's own soft clipping (as in the paper). Op-amp rails as a smooth clamp.
- TL022 swing taken as ±3.5 V about the bias; MN3101 threshold VDD/2; Q5 reset time 0.3 µs.
- The JFET bypass switching (Q6–Q9): the plug-in bypass is the host's.
