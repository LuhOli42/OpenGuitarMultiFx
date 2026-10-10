# Fryette Deliverance D120 — `FryetteDeliveranceD120StyleAmplifier`

## Sources

Fryette's published D120H front-panel layout and owner's documentation; the Deliverance is Steven
Fryette's single-channel ~120 W flagship. Component values are estimated against the documented
topology (two cascaded gain controls, More/Less stage switch, KT88 quad); no official schematic is
public, so resistor values marked "est." are best-fit reconstructions, documented honestly here.

## What the amp actually is

A deliberately raw high-gain head: Gain I doubles as a voicing control (treble emphasis at lower
settings, thickening as it opens — implemented as a bright cap across the pot), Gain II sets drive into
the later stages, and the More/Less switch adds a fourth preamp gain stage. The output section runs
four KT88s at ~520V for tight, authoritative saturation.

## What is modelled

- V1A (100K, 1.8K + 22µF) → .022µF → **Gain I** (1MA + 470pF treble-bleed voicing cap) → V1B (100K,
  2.7K unbypassed) → .022µF → **Gain II** (1MA) → **[V2A "More" stage: 100K, 1.8K + 22µF — switched
  in/out by the front-panel More control]** → V2B (100K, 1.8K + 4.7µF) → cathode follower.
- TMB stack (470pF treble cap, 56K slope, .022 bass/mid, 250K treble / 1M bass / 25K mid) + 1M Master.
- 12AX7 LTP PI (82K/100K plates, NFB + 25K presence) → .047µF couplings → 220K leaks → 4xKT88 pairs,
  fixed bias −55V, ~520V plates.
- KT88 Koren set estimated (kg1 950, kg2 2900, mu 8.6 — no published Koren fit; tuned to idle near the
  real ~40 mA/tube at −55V), supply sag, resonant speaker, `reducedOrder` behavioural stage.

## Deliberate simplifications

- The More/Less switch relocates the optional stage between Gain II and the final stage rather than at
  the end of the chain — same stage count and drive feel, and it keeps the cathode follower's plate
  node fixed (the codebase's established switchable-stage pattern).
- KT88 parameters and NFB resistor are estimates (marked above).
- FX loop, footswitch relays, series/parallel output wiring: not modelled.

## Controls

Page 1 mirrors the front panel: Gain I, Gain II, More (switch), Treble, Mid, Bass, Presence, Master.
Page 2 (synthetic): Power Drive, Bias, Tube Feel, Speaker (4/8/16Ω), Output.

## Stability / verification

- kg1×40 on PI triodes and power pentodes; DC plates ~520V; unity trim for PedalUnityLevel.
- `Tests/FryetteDeliveranceD120StyleAmplifierProcessorTests.cpp`: DC, silence settle, pluck, More
  switch exercised.
