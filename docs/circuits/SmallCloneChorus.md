# Small Clone (Electro-Harmonix Small Clone, vintage MN3007)

Processor: `Source/Effects/Modulation/Chorus/SmallCloneStyleChorusProcessor.{h,cpp}` — key `SmallCloneStyleChorus`.

## Sources
- Aion FX, *Lithium Analog Chorus* documentation (2022) — a parts-exact adaptation of the MN3007 Small Clone, with notes
  on the vintage vs reissue values (R24 27K vintage / 20K reissue, C17 100p vintage / 150p reissue).
- Holters & Parker, DAFx-18 (`BucketBrigadeDelay`).

## What is modelled
| Block | Parts | Model |
|---|---|---|
| Input | C1 33n into R1 220K | 22 Hz high-pass |
| Pre-emphasis (IC1A, non-inverting) | R3 33K, R4 6K8 + C2 10n | (1 + sC2(R3+R4)) / (1 + sC2R4): +15 dB shelf 390 Hz → 2.3 kHz |
| Anti-alias | R11 3K3 / C6 3n3, R12 10K, C8 15n to Q1's emitter, R13 10K / C7 470p | `ladderTransfer` |
| BBD | MN3007, 1024 stages | `BucketBrigadeDelay` |
| Reconstruction | R19 10K / C10 4n7, R20 39K, C11 2n7 to Q2's emitter, R21 39K / C12 180p | `ladderTransfer` |
| Mixer (IC1B) | dry R5 22K and wet C13 + R24 27K meet at one node, R6 10K into the inverting input; feedback R7 33K ‖ (R8 6K8 + C3 10n) | node divider × −Zf/R6 (vintage fixed mix) |

- **LFO** (IC3, LM358 on ~8.7 V): integrator C14 2.2 µ fed through R26 47K + the 1M reverse-log Rate pot (wired as a
  rheostat); Schmitt R27 120K / R28 470K against the R25/R29/R30 taps. The single-supply swing makes the triangle
  slightly asymmetric — kept. Range **~0.42–9.4 Hz**.
- **Clock** (CD4047): D3 resets the RC pin, then it relaxes through R31 39K toward the LFO voltage held on C16 (10 µ),
  starting one VDD above the 4047's VDD/2 threshold (the 4.40·RC astable law); the Q outputs divide by two. The RC pin's
  current back through R31 is included in C16's node equation.

## Assumptions
- **Depth switch**: the original's two positions are not documented anywhere found (Lithium replaced it with a 10KB pot;
  an EHX forum note says a 4.7K stays in the LFO path). Modelled as High = the triangle straight onto C16, Low = through a
  halving divider (~2.5K source, so C16 also smooths it). Unverified — revisit if a factory schematic turns up.
- Supply rails (VA/VC ≈ 8.7 V, VD ≈ 8.1 V) from the 1N5817 + Q3 follower; the bias trimmer's wiper taken as ~25K.
