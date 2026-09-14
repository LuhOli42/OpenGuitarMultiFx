# Circuit Families

A map of every physically-modelled (not neural) pedal/amp circuit implemented in this project, grouped by what they actually share — so a new circuit's research pass has something concrete to check against before writing any code. See each entry's own `docs/circuits/*.md` for the full writeup; this file only tracks *what's related and how*.

Check this file, and the per-circuit docs it points to, before starting a new circuit-modelled effect.

## Single-transistor gain stages (Ebers-Moll + direct nodal analysis)

Circuits with exactly one active device (a BJT) and no separate multi-stage linear network — modelled with a genuine Ebers-Moll transistor (`Source/Effects/EbersMollBJT.h`) plus trapezoidal-discretized capacitor companion models (`Source/Effects/TrapezoidalCapacitor.h`), solved via direct nodal Newton-Raphson. **Not** routed through `chowdsp_wdf`'s generic WDF tree — see [PositiveGroundBooster.md](PositiveGroundBooster.md) for why (no separate sub-network for a tree's composability to earn its keep when every passive part touches one of the transistor's three terminals directly).

| Circuit | Processor | Device | Notes |
|---|---|---|---|
| [Positive Ground Booster](PositiveGroundBooster.md) | `PositiveGroundBoosterProcessor` | 1x PNP | Rangemaster-style treble booster; collector load IS the gain/level control (a pot, not a fixed resistor) |

**What would differ vs. what wouldn't, for a new circuit in this family:** component values, NPN vs. PNP, which terminal(s) have a variable (pot) resistance, whether the collector load is fixed or the gain control itself. What stays the same: the `EbersMollBJT`/`TrapezoidalCapacitor` building blocks, the direct-nodal-analysis architecture, the PNP-mirroring trick if needed. A new single-transistor circuit should very likely reuse both building blocks directly, only changing the Thevenin-network wiring and component values in its own processor `.cpp`.

## Multi-stage tube preamps (not yet implemented)

No circuit in this family exists in this project yet. Researched ahead of time (per the user's request, since this will come up): `chowdsp_wdf` has no triode element. The established real-world approach (Chowdhury-DSP's BYOD `JuniorB`) is a small neural network (RTNeural, 2 in/2 out) trained on real triode I-V curves for the nonlinearity specifically, combined with a **genuine `chowdsp_wdf` R-type WDF tree** for the surrounding reactive network — unlike the single-transistor family above, a real tube preamp usually has enough multi-stage topology (cathode bias networks, coupling stages, tone stacks) that the WDF tree's composability actually earns its keep.

When the first tube circuit comes in: read [PositiveGroundBooster.md](PositiveGroundBooster.md) for the direct-nodal-analysis alternative (still worth considering if the specific tube circuit turns out to be simple/single-stage), but default expectation is a WDF-tree + neural-triode hybrid, following BYOD's validated precedent (read for technique, never copied — GPL-3.0).

## Multi-mic / dynamic cabinet simulation

Not a "circuit" in the SPICE-topology sense (no schematic, no active device) — `DynamicCabProcessor` (see `Source/Effects/AGENTS.md`'s decision log) blends two convolution IRs, optionally level-dependent. Listed here only so it's not confused for a missing family; it doesn't belong in the table above and doesn't need `EbersMollBJT`/`TrapezoidalCapacitor`.
