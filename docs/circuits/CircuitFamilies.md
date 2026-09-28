# Circuit Families

A map of every physically-modelled (not neural) pedal/amp circuit implemented in this project, grouped by what they actually share — so a new circuit's research pass has something concrete to check against before writing any code. See each entry's own `docs/circuits/*.md` for the full writeup; this file only tracks *what's related and how*.

Check this file, and the per-circuit docs it points to, before starting a new circuit-modelled effect.

## Single-transistor gain stages (Ebers-Moll + direct nodal analysis)

Circuits with exactly one active device (a BJT) and no separate multi-stage linear network — modelled with a genuine Ebers-Moll transistor (`Source/Effects/EbersMollBJT.h`) plus trapezoidal-discretized capacitor companion models (`Source/Effects/TrapezoidalCapacitor.h`), solved via direct nodal Newton-Raphson. **Not** routed through `chowdsp_wdf`'s generic WDF tree — see [PositiveGroundBooster.md](PositiveGroundBooster.md) for why (no separate sub-network for a tree's composability to earn its keep when every passive part touches one of the transistor's three terminals directly).

| Circuit | Processor | Device | Notes |
|---|---|---|---|
| [Positive Ground Booster](PositiveGroundBooster.md) ("Rangemaster-Style Booster" in-app — see that doc's trademark note) | `PositiveGroundBoosterProcessor` | 1x PNP | Rangemaster-style treble booster; collector load IS the gain/level control (a pot, not a fixed resistor) |
| [Distortion+-Style Distortion / DOD 250-Style Overdrive](DistortionPlusStyleDistortion.md) | `OpAmpClipperDistortionProcessor` (two models) | 741 op-amp macro-model (finite gain, 1 MHz GBW, rails), 2 germanium / silicon diodes | non-inverting op-amp stage (gain 2-213, C3 bass roll-off) into a 10K + diode pair with a 1 nF cap and the Output pot across it; one block; 1.5-2.1% of a core |
| [Guv'nor-Style Distortion](GuvnorStyleDistortion.md) | `GuvnorStyleDistortionProcessor` | 2 TL072 macro-models, 2 red LEDs | non-inverting gain stage whose pot is also the series resistance into an inverting stage, LEDs to ground, an interactive passive Bass/Middle/Treble stack; two blocks; 3.4% |
| [Blues Breaker-Style Overdrive](BluesBreakerStyleOverdrive.md) | `BluesBreakerStyleOverdriveProcessor` | 2 TL072 macro-models, 4 silicon diodes | non-inverting stage with a two-corner leg, inverting stage with 220K || (6.8K + two diodes in series each way) feedback, passive Tone/Volume; two blocks; 3.9% |
| [RAT-Style Distortion](RatStyleDistortion.md) | `RatStyleDistortionProcessor` | LM308 macro-model (1 MHz GBW), 2 silicon diodes | gain up to 3600x with a two-shelf leg, 1K + 4.7 uF into diodes to ground, passive Filter, JFET follower; one block; 1.9% (no slew limit yet); one class, three models: the original, the RAT 2 and the Turbo RAT (BOM values, red LEDs and an OP07 for the Turbo) |
| [Crunch Box-Style Distortion](CrunchBoxStyleDistortion.md) | `CrunchBoxStyleDistortionProcessor` | 2 LM833 macro-models, 2 red LEDs | the Blues Breaker's arrangement (Drive pot = feedback of stage 1 and series into an inverting stage 2, x6700 total), a 1K + 0.22 uF leg, LEDs to ground behind 1K, passive Tone/shelf/Volume; two blocks; 4.3% |

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
| [OD-1-Style Overdrive](OD1StyleOverdrive.md) | `OD1StyleOverdriveProcessor` | 2x NPN, 2 ideal op-amps (stage 1 NON-inverting, corrected 2026-09-20), one ASYMMETRIC diode pair (1 diode one way, 2 in series the other) | Op-amp 1 = the clipper (diodes across a Drive-pot-controlled feedback resistance); op-amp 2 = fixed-gain (unity) treble-cut buffer, fully linear/closed-form; only 2 real controls (Drive, Level) — no Tone stage, unlike the DS-1 |
| [TS808 / TS9 / TS10-Style Overdrive](TubeScreamerStyleOverdrive.md) | `TubeScreamerStyleOverdriveProcessor` (one class, 3 registered models) | 2x NPN, 2 ideal op-amps, one SYMMETRIC diode pair (1 each way) + a 51 pF cap across it | Op-amp 1 = the non-inverting clipper (diodes + C4 across a Drive-controlled feedback resistance, 1D Newton-Raphson via `AsymmetricDiodePair` with 1/1 diodes); op-amp 2 = closed-form linear tone stage (passive lowpass + pot-controlled shunt); 3 controls (Drive, Tone, Level). TS808/TS9 differ only in two output resistors; the TS10 is modelled from a real schematic (higher Q1 bias, a 220 ohm before the op-amp bias node, extra coupling cap + JFET-bias loading after Level) |

**Symmetric pairs (Tube Screamer family) reuse `AsymmetricDiodePair` with
1/1 diodes rather than `DiodePairT`:** the `DiodePairT` Lambert-W solve is a
WDF-port element; this family's diodes sit in an op-amp feedback network
solved by direct nodal analysis, which needs the plain `I(V)` Newton form
`AsymmetricDiodePair` already provides. Its Newton solve starts from an
analytic upper bound on the root (see its header) -- necessary once the
gain is high enough that a sample can jump from diodes-off to hard
clipping.

**Why a NEW `AsymmetricDiodePair` class, not `chowdsp_wdf`'s `DiodePairT`
again:** `DiodePairT`'s `nDiodes` parameter scales Vt equally on both
sides of the pair — it has no way to express "1 diode this way, 2 the
other," which is specifically what gives circuits in this family (the
OD-1 being the textbook example) their documented asymmetric clipping
character. Checked before writing a new class (per this project's
research-first rule) — genuinely not covered by the vendored library or
by anything else already in this codebase.

**What would differ vs. what wouldn't, for a new circuit in this
family:** diode count/orientation (asymmetric → `AsymmetricDiodePair`; symmetric →
`AsymmetricDiodePair` with 1/1 diodes, as the Tube Screamer does, since the
diodes sit in a nodal-analysis feedback network — `DiodePairT` is for a
shunt clipper inside a WDF tree), feedback
network topology (a bare resistor here; the DS-1/Tube-Screamer lineage
sometimes adds a cap in parallel for extra treble shaping — check the
specific schematic), how many linear buffer/filter op-amp stages surround
the clipper. What stays the same: the "pin the op-amp's virtual-short
voltage, compute the known input current, solve the nonlinear feedback
network for the resulting output" derivation pattern — see the OD-1 doc's
worked-through equation for the template to adapt.

**Before reading any per-circuit doc, read [GainStaging.md](GainStaging.md):** every model here treats a sample as volts,
so how hard it is driven is set outside the model, and that decides whether it sounds like the real thing at all.

## Netlist-solved circuits (`NodalCircuit`)

For circuits that outgrew hand-derived Thevenin chains: stages that *interact*
(feed-forward networks, discrete-transistor op-amps with the gain pot in their
feedback, gyrator filters). A pedal is described as a netlist and solved with
[`NodalCircuit`](NodalCircuitSolver.md) (MNA + trapezoidal capacitors + a
DK-method Newton over device ports; verified against a hand-derived pedal to
0.013%). Cut into blocks wherever an ideal op-amp output / JFET gate / op-amp (+)
pin can't be loaded back.

| Circuit | Processor | Devices | Notes |
|---|---|---|---|
| [Centaur-Style Overdrive](CentaurStyleOverdrive.md) | `CentaurStyleOverdriveProcessor` | 4 ideal op-amps, germanium diode pair | gain stage + 2 feed-forward networks (one through the second Gain gang) + inverting summer + active treble + clean bleed; 5% of a core |
| [BD-2-Style Overdrive](BD2StyleOverdrive.md) | `BD2StyleOverdriveProcessor` | 3 JFET, 4 BJT (2 PNP), 8 diodes, 1 ideal op-amp | two DISCRETE JFET+PNP op-amps with the Gain pot in their feedback, fixed tone stack, two series-diode-pair clippers, transistor gyrator peak filter; 23% of a core |
| [Bassman-Style Amplifier](Bassman5F6A.md) | `BassmanStyleAmplifierProcessor` | 3 x 12AX7, 2 x 12AY7 halves, 2 x 5881 beam tetrodes, output transformer (3 coupled windings), supply model | a whole tube amp: two-channel preamp, passive TMB tone stack, long-tailed-pair phase inverter, push-pull power stage, global feedback with presence, sagging power supply; full reference netlist 18-29% of a core -- **the shipped default is a reduced-order (behavioural) power stage, ~6.15% avg / ~10.7% worst block, zero failures by construction** (see its doc's "Reduced-order power stage" section) |
| [Super Lead-Style Amplifier](SuperLead1959.md) | `SuperLeadStyleAmplifierProcessor` | 4 x 12AX7 halves + 2 x 12AX7 (V3), 4 x EL34 as two pairs, output transformer, supply model | Marshall 1959 100 W from the maker's 2002 drawing: a different two-channel preamp (V1 with separate cathode networks, 470k mixing, bright caps), the Marshall TMB stack (33k, 470 pF), a phase inverter whose cathode tail sits in the feedback node, two EL34 pairs, 47k global feedback with Presence, a choke-filtered chain of five supply nodes; same solver machinery as the Bassman (shared `TubeAmpCommon.h`); full reference netlist ~20-29% of a core, worst block up to ~280% -- **the shipped default is a reduced-order (behavioural) power stage, ~7.7-8.2% avg / ~13.7-15.8% worst block, zero failures by construction** (see its doc's "Reduced-order power stage" section, the first use of this pattern on the project) |
| [HM-2-Style Distortion](HM2StyleDistortion.md) | `HM2StyleDistortionProcessor` | JFET, NPN, PNP, 9 diodes, 5 ideal op-amps | self-biased high-gain transistor stages, asymmetric-diode clipping op-amp, germanium pair in series with the signal, three gyrator followers on the Low/High pots; 18% of a core |
| [DT-1-Style Distortion](DT1StyleDistortion.md) | `DT1StyleDistortionProcessor` | 2x 4558 macro, 2 red LEDs, 3x 4148 | Nobels' own diagram: a JFET input buffer into two op-amp stages driven by ONE 250K pot (its wiper on stage 1's output: more gain in stage 1 and less series resistance into stage 2 at once, ~9400x at the top); red LEDs in stage 1's feedback (±1.55 V), one 4148 against two in series in stage 2's (asymmetric, exactly 2:1); passive Tone; two blocks, 6.2% |
| [ODR-1-Style Overdrive](ODR1StyleOverdrive.md) | `ODR1StyleOverdriveProcessor` | 3x 4558 macro, 4x 4148, a BJT | same maker and template as the DT-1 (shared input/output in `NobelsCommon.h`): a Tube-Screamer-like stage (4148 back to back in the feedback of a two-shelf gain, Drive 250KA with its wiper tied back through 1K8), a shunt diode clipper, a filter, then a stage whose Spectrum pot hangs a bootstrapped-emitter-follower "inductor" (Q2, modelled as a real transistor) on its wiper, then a third stage; three blocks, 6.8% |
| [Overdriver-Style Overdrive](OverdriverStyleOverdrive.md) | `OverdriverStyleOverdriveProcessor` | 3x BC109, no diodes | a discrete-transistor overdrive with no clipping diodes: a directly coupled TR1/TR2 pair (Gain = rheostat in TR1's emitter, 12K AC feedback, 150K DC feedback) into an active Baxandall network that is the feedback of TR3; the transistors clip by themselves; one block, ~3.4% at 1x |
| [EP-Style Booster](EPStyleBooster.md) | `EPStyleBoosterProcessor` | JFET + BJT | the Echoplex EP-3 record preamp: an unbypassed-source JFET stage into a 500K Record Level whose 2 nF bypass lets treble past at any setting (level control acts mostly on the low end), then a feedback-biased NPN stage; one block, ~1.9% |
| [Tube Driver-Style Overdrive](TubeDriverStyleOverdrive.md) | `TubeDriverStyleOverdriveProcessor` | 12AX7 (2 Koren triodes) + 2 op-amps | a 4558 gain stage (500K rheostat, 0-333x) into a zero-bias 12AX7 pair on a +/-14 V supply with 68K plate loads (a "starved plate" ~20 V), then a passive Hi/Lo tone network and Level; two blocks, ~6.2% at 1x; alias is the worst so far (op-amp rail clipping) |
| [Metal Zone-Style Distortion](MetalZoneStyleDistortion.md) | `MetalZoneStyleDistortionProcessor` | 4x M5218 macro + 1 linear, 3 BJT "inductor" branches, JFET, 2x 1SS133 | Boss MT-2 service-manual netlist: a band-pass hump stage (bootstrapped-follower branch on the (-) leg), the Dist stage, a diode clipper into a second hump stage, then an active High/Low + Middle/Mid-Freq EQ; five blocks, ~15% at 1x (over budget), alias worst so far |
| [Zendrive-Style Overdrive](ZendriveStyleOverdrive.md) | `ZendriveStyleOverdriveProcessor` | AD712 macro + an ideal follower, BAT41 / 1N34A / 2N7000 **body diodes** in the feedback | non-inverting stage with Voice (bass corner and gain range) and Drive (Rf 1K .. 500K) and an asymmetric feedback clipper (+0.8 / -1.0 V) built from series chains (`SeriesDiodes.h`: one diode per chain, one port); Tone RC and a buffer; one block; 2.8% |
| [OCD-Style Overdrive](OCDStyleOverdrive.md) | `OCDStyleOverdriveProcessor` | 2 TL082 macro-models, 2 red LEDs, 2N7000 body diodes | two non-inverting stages (up to x460 and x4.85) with a clipper **referenced to the bias**, not ground: 2N7000 body diodes (+-0.67 V) and, with the Clipping switch, LEDs (+-1.7 V); HP/LP bleed and a passive Tone; two blocks; 5.3% |
| [Tone Bender Mk II-Style Fuzz](ToneBenderStyleFuzz.md) (germanium PNP and silicon NPN) | `ToneBenderStyleFuzzProcessor` (one class, 2 models) | 3x BJT | a Fuzz Face pair (Q2+Q3) behind an extra common-emitter stage (Q1); the germanium version gives Q1 NO bias network and relies on the part's leakage (modelled as an explicit collector-base resistance the operating point depends on), the silicon one adds the bias network by hand; reverse-log Attack; ~70 dB of gain, the worst aliasing in the project (-15 dB at 1x, -29 dB even at 4x); 1.3 iterations/sample, 4.2% at 1x |
| [Fuzz Face-Style Fuzz](FuzzFaceStyleFuzz.md) (germanium PNP and silicon NPN) | `FuzzFaceStyleFuzzProcessor` (one class, 2 models) | 2x BJT (PNP+negative rail, or NPN) | directly coupled pair biased by ONE 100k from Q2's emitter to Q1's base (voltage feedback: bias and AC gain together), the Fuzz pot bypassed by 22 uF so it moves gain not bias, split collector load with the output at the junction, and a modelled guitar source resistance because the input impedance is very low; one block, 1.25 iterations/sample, 2.4% of a core |
| [Big Muff-Style Fuzz](BigMuffStyleFuzz.md) (USA V3 and Russian green) | `BigMuffStyleFuzzProcessor` (one class, 2 models) | 4x NPN, 2 anti-parallel silicon pairs | four shunt-feedback-biased stages, the middle two clipping via a diode pair IN THE FEEDBACK path (in series with a cap, so the diodes see no DC and only clip above that cap's corner), passive mid-scoop tone stack; one block, 1.28 Newton iterations/sample, 7.3% of a core |

**Where a diode pair sits decides the family, not how much gain there is:** shunting a gained signal to ground
(DS-1, Distortion+) clips against a wall; sitting in an op-amp's feedback (OD-1, Tube Screamer) or in a
TRANSISTOR stage's feedback (Big Muff, above) collapses that stage's own gain instead, which is a softer, more
progressive squash. Check which one a new schematic is before reaching for a reference implementation.

**What would differ vs. what wouldn't, for a new circuit in this family:** the
netlist, the block cuts, the assumed device parameters. What stays the same:
`NodalCircuit`, the DC-by-relaxation prepare, the dual-mono shortcut pattern in
`process()`, and the per-pedal doc's sections (source, topology, assumptions,
verification, not modelled). Read the solver doc's "Limits" before starting:
ideal op-amps only, no rail clipping.

## Tube amplifiers

The first tube circuit in the project is the [Bassman-Style Amplifier](Bassman5F6A.md), and it took the
direct-nodal route (`NodalCircuit` + Koren tube models), not the WDF-tree + neural-triode hybrid this section
used to predict from BYOD's precedent. Why: the feedback loop from the transformer's secondary back into the
phase inverter, the DC-coupled tone stack, the coupled-inductor output transformer and the supply's sag all
want one exact netlist rather than a WDF tree with a neural network for the tubes; and a triode is just a
two-port device for the existing Newton-over-ports machinery. The neural-triode alternative was not built and
compared; if CPU is what matters later, replacing the tubes' tables by a small network is the place to look
(the tables are already ~3x cheaper than the formulas).

A new tube amp would reuse `TubeModels.h` (12AX7 and 12AY7 sets, a fitted beam tetrode), `addCoupledInductors`,
the supply pattern (a small linear NodalCircuit fed by the power block's tube currents), and the test pattern
(compare stage gains, the open-loop transfer curve and the supply sag against a published analysis).

## Multi-mic / dynamic cabinet simulation

Not a "circuit" in the SPICE-topology sense (no schematic, no active device) — `DynamicCabProcessor` (see `Source/Effects/AGENTS.md`'s decision log) blends two convolution IRs, optionally level-dependent. Listed here only so it's not confused for a missing family; it doesn't belong in the table above and doesn't need `EbersMollBJT`/`TrapezoidalCapacitor`.

## Dynamics, EQ and modulation (added 2026-09-26)

* **Circuit-level (NodalCircuit)**: Dyna Comp / Ross (OTA + rectifier feedback, `DynaCompStyleCompressor.md`), Orange Squeezer (JFET feedback, `SqueezerStyleCompressor.md`),
  GE-7 graphic EQ (gyrators, `GE7StyleEqualizer.md`).
* **Behavioural, calibrated to manuals** (no component data exists): dbx 160 (RMS power averager), G-series bus (dominant peak detector), 1176 (JFET L-section + feedback
  detector), LA-2A (two-stage optical cell + 12AX7 static curve): see `Dbx160StyleCompressor.md`, `GSeriesStyleBusCompressor.md`, `Urei1176StyleCompressor.md`,
  `La2aStyleCompressor.md`. Shared gain law helpers: `Source/Effects/CompressorCommon.h`.
* **Signal processors from the literature**: Ring Mod (Parker diode model, `RingMod.md`), Parametric EQ (RBJ biquads), DS201 gate and NS-2 suppressor (manual specs).
* Mix knob level-matching of the reverbs and delays: `MixLaw.md`.

## Cross-cutting: oversampling
Every clipping pedal that measured audibly aliased at 1x runs through `OversampledEffect`
(2x, DS-1 4x); the table, the method and the CPU trade-off are in
[Oversampling.md](Oversampling.md). A new clipper should be measured the same way before
deciding its factor.

## Cross-cutting: unity level
Every registered pedal is trimmed so that all knobs at noon = bypass loudness for a reference
guitar-like signal -- see [UnityLevel.md](UnityLevel.md). A new pedal needs its trim measured and
added in `EffectRegistry.cpp` (`PedalUnityLevelTests` fails until it is).
