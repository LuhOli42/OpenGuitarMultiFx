# Sunn Model T — `SunnModelTStyleAmplifier`

## Sources

1973 factory schematic ECN441 (Sunn Musical Equipment Co.). ~140 W, four 6550s, ultralinear output stage.

## What the amp actually is

A loud, high-headroom 1970s head: **two** gain stages and a cathode follower before the phase inverter, then four 6550s
with ultralinear screens. It distorts when the Volumes and Master are up and the power stage is pushed -- it is not a
cascaded high-gain preamp, so the add-effect browser files it under **Crunch** (it was under High-Gain).

## What is modelled (ECN441, 9/25/1973, checked 2026-10-27 against `Sunn_model_t.pdf`, el34world)

- **Inputs:** V1A BRITE and V1B NORMAL, R1 / R4 68K stoppers, R2 / R3 1M, R6 / R7 100K plates, and **one shared cathode**,
  R5 680Ω ∥ C1 250µF. The channels differ only after the plates: C2 / C3 .022 → R8 / R9 1M Volumes (linear) → R10 /
  R11 **470K** into V2A's grid, with **C4 470pF across BRITE's 470K**.
- **V2A:** R12 100K, R13 820Ω ∥ **22µF**; DC-coupled into the V2B cathode follower (R14 100K).
- **Tone stack:** C5 270pF, R15 250K treble, R17 56K slope, C6 / C7 .022, R16 1M bass, R18 25K mid -- all linear --
  then R19 1M **Master** (linear) on the treble wiper. The voltage table's conditions ("Bass, Mid, Treble & Master Vol
  set at 10") confirm the Master is a real control.
- **PI:** C8 .022 → V3A; R27 82K / R28 120K plates with C11 270pF between them; the cathodes → R23 470Ω → T → R22 4.7K →
  the feedback node F → R26 10K to ground; R21 / R24 1M grid returns to T; V3B's grid tied to F by C10 0.1µF. DC: tail
  41 V against the chart's 38 V.
- **NFB:** R20 22K from the secondary into F; PRESENCE = C9 .022 from F to the wiper of R25 (25K reverse log), wiper
  to ground. Closing the loop lowers the gain by 12 dB (test).
- **Power:** 4x 6550 as two push-pull pairs, 100K grid leaks to −55 V, 1K stoppers, 1.5K screen resistors to the
  ultralinear taps (`vg2 = (1 − 0.43)·vCT + 0.43·vPlate`), 47Ω plate resistors.
- **Supply:** ~505 V plates through two chokes, sagging.

The first version had: the Normal input on its own 2.7K unbypassed cathode, 100K mix resistors and a 470K leak on
V2A's grid, an unbypassed V2A, audio-taper Volumes / Bass / Master, a 47pF plate cap, the NFB as 10K straight into the
tail (R20 and the R26 shunt missing), the tail resistors swapped, no C10, and Presence wired as a divider. The
reduced path tracked that reference within 18 / 15 dB; against the corrected one it is within 0.6 / 2.8 dB (bound
1.5 / 3).

## Deliberate simplifications

- The **6550 Koren parameters are an estimate**: no published Koren fit exists; the 6L6GC defaults are
  re-fitted (kg1 1100, kg2 3300, mu 8.8, kvb 14) so a pair idles in the real ~40 mA region at −55V/505V.
- The ultralinear screen is updated from the *previous* sample's plate voltage (one-sample delay) —
  standard for this engine; at audio rates indistinguishable.
- Reverb channel, polarity/voltage selector, hum-balance pot: not modelled.

## Controls

Page 1 is the front panel: Volume Brite, Volume Normal, Treble, Mid, Bass, Presence, Master. Page 2 (synthetic): Power
Drive, Bias, Tube Feel, Speaker (4/8/16Ω), Output. Unity trim −9.06 dB.

## Stability / verification

- kg1×40 on PI triodes and power pentode pairs -- REMOVED (it was the reference-collapse mechanism;
  see CircuitFamilies.md's dead-reference repair section). The PI's second grid leak was also moved
  from the ~0 V feedback node to the tail midpoint (it had sat ~27 V below the cathode = cutoff), and
  the tail now returns through the feedback node, same shape as the Super Lead.
- DC: plates ~505V; unity trim calibrated for the PedalUnityLevel suite.
- `Tests/SunnModelTStyleAmplifierProcessorTests.cpp`: DC convergence, silence settle, pluck, both
  channels and the More/Normal volume interaction.
