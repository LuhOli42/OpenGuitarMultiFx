# Positive Ground Booster (`PositiveGroundBoosterProcessor`)

A Rangemaster-style single-transistor "positive ground" treble booster, modelled directly from General Guitar Gadgets' GEB schematic: https://generalguitargadgets.com/pdf/ggg_geb_pos_sc.pdf ("GEB - Rangemaster™ Positive Ground Booster", JD Sleep, 2012).

This is the project's first *physically modelled* (not neural) circuit — see root `AGENTS.md`'s Global Decisions for the `chowdsp_wdf` dependency choice this grew out of. Read this file to understand how the circuit works before touching the code; don't re-derive it from the `.cpp` each time.

## What the real circuit does

Guitar in → C1 (a tiny 0.0047µF input cap, deliberately rolling off bass — this is *the* Rangemaster character) → a single PNP transistor common-emitter gain stage, with the collector load itself being a variable resistor (the "Boost" pot) → C2 output coupling. The emitter resistor (R4) is fully bypassed by C4, so there's no local negative feedback — gain is high, and the stage saturates readily, which is the point.

Power is "positive ground": circuit ground is the battery's `+` terminal, so the supply rail (reached through D1, a reverse-polarity-protection diode) sits at a **negative** voltage relative to ground — unusual to read at first, but it's exactly what makes a PNP transistor the right device here (emitter near ground/most-positive, collector toward the negative rail/most-negative).

| Component | Value | Role |
|---|---|---|
| C1 | 0.0047µF | Input coupling — the small value is why this circuit is bright/treble-forward |
| R1 | 470kΩ | Base bias, to supply |
| R2 | 68kΩ | Base bias, to ground |
| R4 | 3.9kΩ | Emitter resistor |
| C4 | 47µF | Emitter bypass — fully bypasses R4 at audio frequencies, no local feedback |
| R3 | 10kΩ log pot | **Collector load** — this is the "Boost" control; varying it directly sets gain |
| C2 | 0.01µF | Output coupling |
| D1 | 1N914 | Reverse-battery protection, in series with the supply |
| Q1 | PNP (unspecified part) | The gain stage |

Sw1a/b/c, R6, D2 are the 3PDT true-bypass footswitch and LED indicator — hardware/UI concerns, not part of the audio circuit, not modelled here.

## Modelling approach

**Genuine large-signal Ebers-Moll transistor + trapezoidal-discretized (bilinear) companion models for every capacitor, combined via direct nodal analysis, solved with 3D Newton-Raphson every sample.** Not `chowdsp_wdf`'s generic WDF tree, and not a simplified saturating-curve approximation of the transistor.

### Why not the generic WDF tree

Researched first (per the project's now-standing "research before implementing" convention) — Chowdhury-DSP's own BYOD has a near-identical circuit (`RangeBooster.cpp`, GPL-3.0, read for technique only, never copied). They don't route it through the generic WDF tree either: same reasoning applies here. Every passive component in this circuit connects directly to one of the transistor's three terminals — there's no separate multi-stage linear sub-network for a generic adaptor tree's composability to earn its keep. A hand-derived nodal solve is just as numerically accurate (the same trapezoidal/bilinear discretization a WDF capacitor uses internally) and more auditable: the exact KCL equations can be written out and checked by hand, rather than trusting a from-scratch 2-port BJT R-type adaptor with no independent way to verify it.

### Why not a simplified transistor curve

Explicitly rejected by the user in favor of maximum circuit fidelity: "não quero apenas um efeito que soe parecido... quero uma simulação baseada no circuito real, tentando reproduzir o comportamento elétrico dele da forma mais fiel possível." The transistor is the genuine Ebers-Moll model (`EbersMollBJT.h`), not a calibrated tanh/waveshaper approximation.

### The circuit reduces to

Three Thevenin-equivalent networks, one per transistor terminal, each combining the fixed bias resistors with the relevant capacitor's trapezoidal companion model (`TrapezoidalCapacitor.h`):

- **Base**: R1 (to supply) ‖ R2 (to ground) ‖ C1's companion model (in series with the external input signal — this one has an extra series source, which changes the sign of how its history term folds in; see the code comment where this was originally gotten wrong).
- **Emitter**: R4 ‖ C4's companion model, both to ground.
- **Collector**: R3 (the boost pot, to supply) ‖ [C2's companion model in series with an assumed downstream load resistance, to ground].

`EbersMollBJT::solve()` takes these three (resistance, Thevenin voltage) pairs and solves the coupled nonlinear system for all three terminal voltages simultaneously (full 3-terminal treatment — no terminal is simplified to an "ideal, non-loading" source the way BYOD's own version does for its base node; this is deliberately *more* rigorous than that reference, per the fidelity-over-ease priority).

### PNP handling

`EbersMollBJT` only ever implements the standard NPN equations. PNP is handled by mirroring every voltage the processor feeds in (negate) and mirroring every voltage it gets back (negate again) — a PNP transistor is exactly a mirror-image NPN in negated coordinates. Keeps the numerically-sensitive Newton-Raphson code with only one polarity to get right and verify.

## Modelling assumptions (explicit, not hidden)

The schematic doesn't pin these down — documented here so they're easy to find and refine later:

- **Transistor parameters**: `Is = 5pA`, `Vt = 25.85mV`, `βF = 200`, `βR = 4` — representative small-signal silicon PNP values (the GGG kit doesn't specify an exact part; various small-signal PNPs are commonly used in real builds). Set in `PositiveGroundBoosterProcessor`'s constructor via `EbersMollBJT::setParameters()` — trivial to swap for a specific transistor's real datasheet/measured values later.
- **Supply voltage**: 8.4V (9V battery minus D1's ~0.6V forward drop, a standard approximation — C3's 47µF supply filter cap makes this a slow DC bias parameter with no audio-rate interaction, so the exact diode-current-dependent drop doesn't need per-sample modelling).
- **Downstream load impedance**: 1MΩ (a standard assumption for "whatever this feeds next" — a following pedal or amp input — since this runs in a digital chain with no literal physical load).
- **Boost pot taper**: the schematic specifies "10k Log" (audio taper); approximated as `R3 = 10kΩ × knob²`, a common simple stand-in for a true logarithmic taper (real log pots aren't pure log curves either — this is a documented approximation, not a corner cut for accuracy's sake elsewhere).

## Verified behaviour (2026-09-14, at implementation time)

Checked in `Tests/PositiveGroundBoosterProcessorTests.cpp` — not just "doesn't crash," which a transistor stuck at cutoff would also pass:

- **DC operating point**: `Vbe ≈ -0.44V` (base 0.44V below emitter — correctly signed forward bias for a PNP), collector sitting ~0.4V off the supply rail (real current flowing, not pinned at cutoff).
- **Boost knob gain sweep** (1kHz small-signal): 0.05x → 1.99x → 7.97x → 17.90x → 31.68x across the knob's range — smoothly monotonic, and ~30x (≈30dB) at maximum matches published descriptions of this circuit family's typically large boost.
- Finite/bounded output under a loud (0.5 amplitude) input at every Boost setting, and with silence.

## A real bug found during verification, worth knowing about

The base node's Thevenin voltage initially had the sign on C1's history term backwards (`x + histC1` instead of `x - histC1`), because C1 has an *external ideal source in series with it* (the guitar input), unlike C4/C2 which connect directly between their node and a fixed rail. This is a subtle, easy-to-get-wrong distinction: a capacitor's companion-model history term folds into a KCL equation with the OPPOSITE sign depending on whether it's a "direct" branch (source and sink are the same two nodes you're solving for) or has an extra ideal source riding in series with it. It only surfaced because the bias-point test checked the actual solved voltages (transistor was stuck at cutoff — a perfectly finite, stable, *wrong* answer) rather than only checking boundedness. **Worth re-deriving this sign carefully, by hand, with a `hist=0` plain-resistor sanity check, for any future series-source-plus-capacitor branch in a new circuit** — don't assume the same sign as a "direct" branch.

## Reusable building blocks this circuit produced

For future circuits (see the project's circuit-modeling-reuse-components convention):

- `Source/Effects/TrapezoidalCapacitor.h` — generic bilinear-discretized capacitor companion model.
- `Source/Effects/EbersMollBJT.h` — generic 3-terminal Ebers-Moll BJT, 3D Newton-Raphson, NPN-native with PNP handled by the caller mirroring in/out.

Not reusable as-is, but worth knowing about for a *tube* circuit when one comes up: `chowdsp_wdf` has no triode element either, and the established real-world approach (BYOD's `JuniorB`) is a small trained neural network for the triode's nonlinearity, combined with a genuine WDF R-type tree for the surrounding (usually more complex, multi-stage) reactive network — see `docs/circuits/CircuitFamilies.md`.
