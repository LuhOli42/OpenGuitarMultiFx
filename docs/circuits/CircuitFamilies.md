# Circuit Families

A map of every physically-modelled (not neural) pedal/amp circuit implemented in this project, grouped by what they actually share — so a new circuit's research pass has something concrete to check against before writing any code. See each entry's own `docs/circuits/*.md` for the full writeup; this file only tracks *what's related and how*.

Check this file, and the per-circuit docs it points to, before starting a new circuit-modelled effect.

## Single-transistor gain stages (Ebers-Moll + direct nodal analysis)

Circuits with exactly one active device (a BJT) and no separate multi-stage linear network — modelled with a genuine Ebers-Moll transistor (`Source/Effects/EbersMollBJT.h`) plus trapezoidal-discretized capacitor companion models (`Source/Effects/TrapezoidalCapacitor.h`), solved via direct nodal Newton-Raphson. **Not** routed through `chowdsp_wdf`'s generic WDF tree — see [PositiveGroundBooster.md](PositiveGroundBooster.md) for why (no separate sub-network for a tree's composability to earn its keep when every passive part touches one of the transistor's three terminals directly).

| Circuit | Processor | Device | Notes |
|---|---|---|---|
| [Positive Ground Booster](PositiveGroundBooster.md) ("Rangemaster-Style Booster" in-app — see that doc's trademark note) | `PositiveGroundBoosterProcessor` | 1x PNP | Rangemaster-style treble booster; collector load IS the gain/level control (a pot, not a fixed resistor) |

**What would differ vs. what wouldn't, for a new circuit in this family:** component values, NPN vs. PNP, which terminal(s) have a variable (pot) resistance, whether the collector load is fixed or the gain control itself. What stays the same: the `EbersMollBJT`/`TrapezoidalCapacitor` building blocks, the direct-nodal-analysis architecture, the PNP-mirroring trick if needed. A new single-transistor circuit should very likely reuse both building blocks directly, only changing the Thevenin-network wiring and component values in its own processor `.cpp`.

## Multi-stage transistor/op-amp distortion (direct nodal analysis + one minimal `chowdsp_wdf` adaptor)

Circuits with several active devices in series (BJTs, JFETs, an op-amp
gain stage) plus a diode clipper, where at least one device needs a
technique the single-transistor family above doesn't: a JFET (no native
`chowdsp_wdf` element, same gap BJTs had — see
`Source/Effects/ShichmanHodgesJFET.h`), an op-amp stage solved in closed
form (ideal/virtual-short, not iterative — valid specifically *because*
it's an op-amp, not a transistor, see the DS-1 doc's explanation), and/or
a shunt diode-pair clipper, which — unlike the BJT/JFET gap — **is**
something `chowdsp_wdf` already provides (`DiodePairT`, Werner et al.'s
Lambert-W solve) and should be reused via one minimal WDF adaptor pair
(a `ResistiveVoltageSourceT` feeding the `DiodePairT`) rather than
reinvented, even though the rest of the circuit stays direct-nodal-
analysis (no full WDF tree — same reasoning as the single-transistor
family: no separate sub-network complex enough for a tree's composability
to earn its keep here either).

| Circuit | Processor | Devices | Notes |
|---|---|---|---|
| [DS-1-Style Distortion](DS1StyleDistortion.md) | `DS1StyleDistortionProcessor` | 3x NPN, 1x JFET, 1 ideal op-amp, anti-parallel diode pair | 2-stage NPN pregain (one is a plain emitter follower, the other has collector-to-base shunt feedback — see that doc for the one-sample-delayed treatment this needs), a JFET used as a voltage-controlled resistor (not a gain stage — biased at Vgs≈0), a non-inverting op-amp gain stage (pot-controlled, closed-form), Distortion+-style anti-parallel diode-to-AC-ground clipping, a passive Big Muff-style tone stack, third NPN output buffer |

**What would differ vs. what wouldn't, for a new circuit in this
family:** device count/types, which stage(s) are nonlinear vs. which
reduce to closed-form linear algebra, exact pot/feedback wiring. What
stays the same: `EbersMollBJT`/`ShichmanHodgesJFET`/`TrapezoidalCapacitor`
for the transistor stages, the "ideal op-amp closed-form, no Newton-
Raphson" treatment for any op-amp gain stage, `chowdsp_wdf`'s `DiodePairT`
for any shunt/series diode clipper rather than a hand-rolled solve.

## Op-amp diode-in-feedback clippers (direct nodal analysis, 1D Newton-Raphson)

Circuits whose core distortion mechanism is diode(s) placed directly in
an op-amp's own negative feedback loop (not shunting a separately-gained
signal to ground, which is the DS-1/Distortion+ family above) — the
classic topology the BOSS OD-1 originated and the Ibanez Tube Screamer
made famous two years later. The feedback network being nonlinear means
the op-amp stage can't be solved in closed form the way the DS-1's linear-
feedback stage can; it needs a genuine (if small — one unknown) Newton-
Raphson solve, same category of problem as a transistor's KCL. Input/
output buffering around the clipper is plain `EbersMollBJT` emitter
followers, same as every other family here.

| Circuit | Processor | Devices | Notes |
|---|---|---|---|
| [OD-1-Style Overdrive](OD1StyleOverdrive.md) | `OD1StyleOverdriveProcessor` | 2x NPN, 2 ideal op-amps, one ASYMMETRIC diode pair (1 diode one way, 2 in series the other) | Op-amp 1 = the clipper (diodes across a Drive-pot-controlled feedback resistance); op-amp 2 = fixed-gain (unity) treble-cut buffer, fully linear/closed-form; only 2 real controls (Drive, Level) — no Tone stage, unlike the DS-1 |

**Why a NEW `AsymmetricDiodePair` class, not `chowdsp_wdf`'s `DiodePairT`
again:** `DiodePairT`'s `nDiodes` parameter scales Vt equally on both
sides of the pair — it has no way to express "1 diode this way, 2 the
other," which is specifically what gives circuits in this family (the
OD-1 being the textbook example) their documented asymmetric clipping
character. Checked before writing a new class (per this project's
research-first rule) — genuinely not covered by the vendored library or
by anything else already in this codebase.

**What would differ vs. what wouldn't, for a new circuit in this
family:** diode count/orientation (symmetric → reuse `chowdsp_wdf`'s own
`DiodePairT` instead; asymmetric → `AsymmetricDiodePair`), feedback
network topology (a bare resistor here; the DS-1/Tube-Screamer lineage
sometimes adds a cap in parallel for extra treble shaping — check the
specific schematic), how many linear buffer/filter op-amp stages surround
the clipper. What stays the same: the "pin the op-amp's virtual-short
voltage, compute the known input current, solve the nonlinear feedback
network for the resulting output" derivation pattern — see the OD-1 doc's
worked-through equation for the template to adapt.

## Multi-stage tube preamps (not yet implemented)

No circuit in this family exists in this project yet. Researched ahead of time (per the user's request, since this will come up): `chowdsp_wdf` has no triode element. The established real-world approach (Chowdhury-DSP's BYOD `JuniorB`) is a small neural network (RTNeural, 2 in/2 out) trained on real triode I-V curves for the nonlinearity specifically, combined with a **genuine `chowdsp_wdf` R-type WDF tree** for the surrounding reactive network — unlike the single-transistor family above, a real tube preamp usually has enough multi-stage topology (cathode bias networks, coupling stages, tone stacks) that the WDF tree's composability actually earns its keep.

When the first tube circuit comes in: read [PositiveGroundBooster.md](PositiveGroundBooster.md) for the direct-nodal-analysis alternative (still worth considering if the specific tube circuit turns out to be simple/single-stage), but default expectation is a WDF-tree + neural-triode hybrid, following BYOD's validated precedent (read for technique, never copied — GPL-3.0).

## Multi-mic / dynamic cabinet simulation

Not a "circuit" in the SPICE-topology sense (no schematic, no active device) — `DynamicCabProcessor` (see `Source/Effects/AGENTS.md`'s decision log) blends two convolution IRs, optionally level-dependent. Listed here only so it's not confused for a missing family; it doesn't belong in the table above and doesn't need `EbersMollBJT`/`TrapezoidalCapacitor`.
