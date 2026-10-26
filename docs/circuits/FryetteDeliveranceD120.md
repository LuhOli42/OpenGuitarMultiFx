# Fryette Deliverance D120 — `FryetteDeliveranceD120StyleAmplifier`

## Sources

Fryette's published D120H front-panel layout and owner's documentation; the Deliverance is Steven
Fryette's single-channel ~120 W flagship. Component values are estimated against the documented
topology (two cascaded gain controls, More/Less stage switch, KT88 quad); no official schematic is
public, so resistor values marked "est." are best-fit reconstructions, documented honestly here.

**Power-section topology.** Fryette never published a schematic. The only VHT / Fryette drawing that circulates is
SLOclone's reverse-engineered Deliverance 60. Music-Electronics-Forum posts based on it describe the layout:

- the 60 is a gain stage → a **cathodyne** → the power tubes;
- the 120 adds **cathode-follower drivers** after the cathodyne.

The model follows that layout. The SLOclone drawing itself and a PedalPCB build thread's schematics need a login to
download, so all values in the section are estimated.

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
- **Phase inverter and drivers** (all estimated):
  - **Gain stage:** 12AX7 with a .022 µF / 1M input, 100K plate, and 1.5K cathode over a 470 Ω node. The NFB (56K
    from the speaker), the 25K Presence and the Depth LF shunt all land on that node.
  - **Cathodyne:** 12AX7, AC-coupled, 100K plate, 1.5K bias + 100K load cathode, with its 1M grid leak bootstrapped
    to the 1.5K / 100K joint.
  - **Drivers:** two 12AU7 cathode followers. Each takes .047 µF in, with a 220K leak on the bias node. Their plates
    sit behind a 33K / 22 µF dropper, and their 47K cathode loads go to −150 V. They drive the 4×KT88 grids directly
    through 1.5K stoppers.
  - **Bias:** the bias supply is −76 V, which puts the KT88 grids at about −48 V. Plates are ~520 V.
  - **Output transformer polarity:** the winding sense is set so the cathode-return NFB is negative. With the other
    sense, silence does not settle.
- KT88 Koren set estimated (kg1 950, kg2 2900, mu 8.6 — no published Koren fit; tuned to idle near the
  real ~40 mA/tube at −55V), supply sag, resonant speaker, `reducedOrder` behavioural stage.

## Deliberate simplifications

- The More/Less switch relocates the optional stage between Gain II and the final stage rather than at
  the end of the chain — same stage count and drive feel, and it keeps the cathode follower's plate
  node fixed (the codebase's established switchable-stage pattern).
- KT88 parameters and NFB resistor are estimates (marked above).
- FX loop, footswitch relays, series/parallel output wiring: not modelled.

## Controls

Page 1 mirrors the front panel: Gain I, Gain II, More (switch), Treble, Mid, Bass, Presence, Depth, Master.
The Depth control (variable LF damping) was missing in the first model; it is an R+L low-frequency shunt
on the NFB node in the full netlist and an LF-drive term on the fitted tail at reduced order -- Fryette
publishes no schematic, so the standard resonance implementation is used and documented.
Page 2 (synthetic): Power Drive, Bias, Tube Feel, Speaker (4/8/16Ω), Output.

## Stability / verification

- DC plates ~520 V, KT88 grids ~−48 V. The 10 s silence settle peaks at 0.0125.
- **Reduced stage:** refitted after the rebuild (HF shelf 0.55 → 1.3). It tracks within 1.98 dB of level and
  1.94 dB of band shape (bound 3 / 4).
- **Trim:** −9.49 dB, giving unity.
- `Tests/FryetteDeliveranceD120StyleAmplifierProcessorTests.cpp`: DC, silence settle, pluck, More
  switch exercised.
