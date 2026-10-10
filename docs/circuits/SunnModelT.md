# Sunn Model T — `SunnModelTStyleAmplifier`

## Sources

1973 factory schematic ECN441 (Sunn Musical Equipment Co.). ~140 W, four 6550s, ultralinear output stage.

## What the amp actually is

Sunn's flagship. Two independent input channels — **Brite** (hot: 680Ω cathode + 250µF bypass) and
**Normal** (unbypassed cathode) — each with its own Volume, mixed into a shared second stage, a
mid-forward tone stack built around a small 270pF treble cap, a 12AX7A long-tailed-pair PI, and the
signature output stage: four 6550s with **ultralinear screens** — the screens connect to taps on the
output-transformer primary (~43% from the CT), so the screen voltage follows the plate swing. That
local feedback inside the output stage is why a Model T stays articulate and loud far past the point
where a pentode-wired amp turns to mush. Stoner/doom's reference amp for a reason.

## What is modelled

- Both input channels (driven together like the "both" jack): V1A Brite (R6 100K plate, R5 680Ω + C1
  250µF) → C2 .022µF → 250K Volume → 100K mix resistor; V1B Normal (R7 100K, unbypassed cathode —
  estimated 2.7K) → C3 .022µF → 250K Volume → 100K mix resistor → V2A shared stage (100K, 820Ω R13,
  unbypassed) → cathode follower.
- Tone stack per ECN441: C5 **270pF** treble cap, R17 56K slope, C8/C7 .022µF, Treble 250K (R15),
  Bass rheostat (R16), Mid 25K (R18), 1M Master.
- PI: 12AX7A LTP, R27 82K / R28 120K plates, R22 4.7K + R23 470Ω tail, .025µF couplings (C12/C13),
  .022µF input cap, grid-2 NFB through R24 1M.
- NFB: R26 10K from the secondary + presence network (R25 25K REV-LOG shunt, C9 .022µF).
- Power: 4x6550 as two push-pull pairs, 100K grid leaks (R29/R30) to the −55V bias rail (E -55V test
  point), 1K-per-tube stoppers (R31–R34 → 500Ω per pair), ultralinear screens applied per sample:
  `vg2 = (1 − 0.43)·vCT + 0.43·vPlate` from the previous sample's plate.
- OT: 4/8/16Ω taps, ~3.5H primary half; supply ~505V plate rail through dual chokes, sagging dynamically.
- `reducedOrder` behavioural power stage when the quality tier demands it.

## Deliberate simplifications

- The **6550 Koren parameters are an estimate**: no published Koren fit exists; the 6L6GC defaults are
  re-fitted (kg1 1100, kg2 3300, mu 8.8, kvb 14) so a pair idles in the real ~40 mA region at −55V/505V.
- The ultralinear screen is updated from the *previous* sample's plate voltage (one-sample delay) —
  standard for this engine; at audio rates indistinguishable.
- Reverb channel, polarity/voltage selector, hum-balance pot: not modelled.
- Normal-channel cathode (2.7K) is an estimate — the schematic's second-half cathode network is only
  partially legible.

## Controls

Page 1 mirrors the front panel: Volume Brite, Volume Normal, Treble, Mid, Bass, Presence, Master.
Page 2 (synthetic): Power Drive, Bias, Tube Feel, Speaker (4/8/16Ω), Output.

## Stability / verification

- kg1×40 on PI triodes and power pentode pairs.
- DC: plates ~505V; unity trim calibrated for the PedalUnityLevel suite.
- `Tests/SunnModelTStyleAmplifierProcessorTests.cpp`: DC convergence, silence settle, pluck, both
  channels and the More/Normal volume interaction.
