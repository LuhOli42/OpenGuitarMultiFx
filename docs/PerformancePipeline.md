# Performance pipeline: making circuit models cheap without changing the sound

This is the project's standard way to bring a component-level pedal (a `NodalCircuit` netlist solved with Newton every
sample) down to the 5 %-of-a-core budget (see `docs/circuits/` for each pedal). The first pedal converted is the Big
Muff family; the recipe below is what worked there, with the numbers.

## The principles

| # | Principle | Status in the Big Muff |
|---|---|---|
| 1 | **No iterative solver on the audio path.** Constant cost per sample, no data-dependent iteration count, no failure path. | Done: no circuit-level Newton. Each nonlinearity is solved alone with a fixed number of steps. |
| 2 | **Coefficients off the sample loop.** Anything that depends only on knobs is computed when a knob moves. | Done: the network matrix is inverted (and compiled) only when a pot changes. |
| 3 | **Static curves instead of computing the physics per sample.** Device characteristics are tabulated once. | Done: diode pair `2 Is sinh(v/nVt)` and each transistor's exponential are tables (`DeviceCurve`, `TransferCurve`). |
| 4 | **Float32 in the hot loop, double where it matters.** | Done: state and compiled matrices are float; matrix inversion, curve building and the port solves stay double. |
| 5 | **Process stage by stage; oversample only the nonlinear stages.** | Not applicable as such: in a Big Muff every stage loads the one before it, so the circuit is one coupled network, not a chain of separable blocks. The whole pedal stays inside the 2x oversampler. Pedals with buffered stages (op-amp outputs) *can* be split, and should be. |

Measured effect of each step on the Big Muff (bench `EffectCostBench`, same 2x oversampling, pickup load and trim as the
shipped block):

| Step | Cost (% of one core) |
|---|---|
| Newton netlist (before) | 11.1 |
| Linear network + ports, first version | 7.6 |
| + state-space compile, warm-started ports, dropped ports that did not change the result | 5.7 |
| + diode characteristic from a table | 5.0 |
| + float32 hot loop | 4.9 (x86; the float gain is for the ARM target: NEON is 4 floats wide, 2 doubles) |

**Result: 11.1 % -> 4.9 % (2.3x), the bare processor 3.6x**, with the sound matched to the netlist (below).

## How it works: `pipeline::LinearPortNetwork`

`Source/Effects/Pipeline/LinearPortNetwork.h`, a fixed-capacity, allocation-free, copyable class.

1. **Linear part.** Every resistor and capacitor of the netlist is kept (capacitors as trapezoidal companions). Each
   transistor becomes its hybrid-pi at the DC operating point, computed once from the bias network (the Big Muff's
   `shuntFeedbackPoint` / `dividerPoint`), with the emitter degeneration folded in: input resistance
   `r_pi + (beta+1) Re`, transconductance `gm / (1 + gm Re (beta+1)/beta)`. Op-amps would be their linear macro.
2. **Compile.** When a resistance changes (a pot moved, checked at control rate), the conductance matrix is inverted
   and compiled into state-space form over exactly what the sample loop reads: the capacitor voltages, the port
   voltages and the output. A sample then costs about (caps + ports + outputs) x caps multiply-adds, not nodes^2.
3. **Ports** are the only nonlinear parts. Each is solved on its Thevenin equivalent `v + Z * I(v) = p`:
   - diode pairs (`addDiodePair`, with a `DeviceCurve` table);
   - transfer ports (`addTransferPort`) for a transistor's exponential: control at the base, current injected at the
     collector, the tabulated departure from the linear slope; this is where the stages' even harmonics come from;
   - swing limits (`addClamp`): exactly zero current inside the stage's swing window (so the loop skips them), soft
     exponential beyond it, like cutoff / saturation.

   `f(v)` is strictly increasing, so a Newton step from the far side of the root converges monotonically. Every port
   starts from the previous sample's solution when it is on that side (signals move little between samples), else
   from a closed-form far-side guess; the step count is fixed (2 or 3). Couplings between ports use this sample's
   current for ports already solved, last sample's for the rest.
4. **Inject** the port currents back through the compiled columns and advance the capacitor states.

### What to keep and what to drop
Measure, do not guess: mask the ports out one kind at a time in the bench (the Big Muff profile was linear part
1.5 %, diodes +1.9 %, transistor exponentials +1.0 %, swing limits +1.2 % at 1x) and drop the ones that do not move
the equivalence numbers. In the Big Muff:
- kept: both diode pairs, the exponential of the input and both clipping transistors, swing limits on the input and
  output stages, **the diodes' 2 pF junction capacitance** (x stage gain it is comparable to the 470 pF feedback cap;
  leaving it out made the top octave 0.8 dB too bright);
- dropped: the output stage's exponential (its input is after the tone stack, small signal), the diodes' diffusion
  capacitance, swing limits on the clipping stages (their diodes hold the collectors inside 1 V).

## The test a conversion must pass before it replaces the original

`Tests/PipelineEquivalenceTests.cpp`, against the Newton netlist (kept in the class as the reference):

1. **Level and timbre**: a decaying three-note chord at two input levels, across the models and knob corners. Level
   within 2 dB, every audible octave band (level removed) within 1.2 dB. Big Muff: **0.78 dB / 0.73 dB**.
2. **Transients** (what a level/spectrum test misses; a past reduction passed those and still popped on hard
   edges): hard-gated chord bursts. Peak level within 0.6 dB, every 5 ms envelope frame within 1.5 dB through attack,
   sustain and cut-off (frames down to -40 dB), onsets in the same 5 ms frame. Big Muff: **peak 0.38 dB, envelope
   1.48 dB (25 ms after a cut-off, 40 dB down: the series-cap discharge of the Russian), onset 0 frames**.
3. **Robustness**: random knob moves with 3 V bursts stay finite and bounded, the dual-mono shortcut's channel split
   is clean, silence settles to silence.
4. **CPU**: it must be measurably cheaper (the same test times both engines).
5. **Unity level** (`PedalUnityLevelTests`): the registered block stays within 1 dB of unity with the existing trim.
   Big Muff: -0.00 / -0.08 / -0.08 dB.

When all of it passes, the original registry key switches to the new engine (no separate A/B block); the netlist
stays in the class for the test.

## The netlist compiler: `pipeline::CompiledCircuit` (2026-10-17)

Converting a pedal by hand (the Big Muff) means retyping its circuit. `Source/Effects/Pipeline/CompiledCircuit.h`
compiles an existing `NodalCircuit` block instead, at its DC operating point (`NodalCircuit::describe()` exports the
elements and the solved DC voltages):

- fixed sources (supply, bias rails) become AC ground; the block's signal source becomes an input constraint row;
- resistors keep their netlist handles in the same order, so the pedal's `updatePots()` drives both engines unchanged;
  capacitor handles are mapped (`setCapacitance`, e.g. the Bass Big Muff's Bass Boost switch);
- op-amp / follower rows stay constraint rows. An op-amp macro's saturating output follower becomes a 1-ohm source with a
  node rail-limit port, and its integrator clamp diodes become the eco macro's 470-ohm anti-windup bleed
  (`addOpAmpMacroLite`): one port per op-amp, and the output still sticks to the rail while the feedback network keeps
  charging, which is what gives a high-gain stage its recovery tail after the note stops;
- BJTs and JFETs are their small-signal Jacobians at the DC point. `transistorFull` instead makes each two-port BJT a
  pair of coupled ports (vbe, vbc) -> (ib, ic), full Ebers-Moll with both junctions, solved by a 2x2 Newton with SPICE
  pnjlim limiting (only steps that take a junction above its critical voltage are compressed; a reverse collector
  junction must swing volts per sample). Exact enough for deep saturation (HM-2) but costly: use it only where the
  netlist it replaces is expensive. A BJT can also get a transfer port carrying its
  exponential with Vce(sat) folded in (`CompileOptions::transistorExponential`), only where the equivalence test shows
  it pays: on the Bass Big Muff it made things worse, so it is off by default;
- diodes: series chains through an otherwise unconnected node are merged (one exponential of summed nVt), every set of
  unbiased diodes on the same node pair becomes ONE composite port with a tabulated curve (its range set by each
  diode's 1 A point: red LEDs need ~2 V, silicon ~0.9 V), biased ones are single exponential ports;
- inductors (single winding: the same trapezoidal state as a capacitor, conductance T/2L, history reversed) and up to
  two signal inputs per block (a block fed from two blocks, e.g. a dry/wet blend);
- port order (Gauss-Seidel) matters when one port drives another hard: `railsFirst` solves the op-amp rail limits
  before the diodes. A clipper behind a rail-pinned op-amp needs it (the RAT's clipper read 2.4 V on a silicon pair
  without it); the OCD measures worse with it, so it is chosen per pedal. More sweeps did not fix those cases and cost
  the whole gain (3 sweeps: OCD x1.0);
- exponentials are continued as straight lines above e^40 (SPICE junction limiting) so a port equation always has a
  root however hard a 100 dB open-loop gain pushes it; far-side Newton starts are computed in the log domain.

The pedal keeps its netlist build and DC solve; `setPipelineEngine (false)` switches back to it (the tests do, both as
the reference and for the circuit-model tests that probe internal nodes). Per sample the cost is a few small dot
products per block (written with four accumulators so they vectorise: that alone took the Bass Big Muff from 5.9 % to
3.5 %) plus the active ports.

## Batch results (2026-10-17)

Bench `EffectCostBench`, % of one core with each block's own oversampling, before -> after. Equivalence against the
full Newton netlist (`Tests/PipelineEquivalenceTests.cpp`, `CompiledPipeline`), worst over three knob settings, two chord
levels and two burst levels:

| Pedal | Cost | Level | Band | Peak | Envelope | Status |
|---|---|---|---|---|---|---|
| Big Muff / Russian / Sovtek | 11.1 -> 4.1 | 0.78 | 0.73 | 0.38 | 1.48 | applied (hand-built network) |
| Bass Big Muff | 13.8 -> **3.8** | 0.44 | 0.44 | 0.30 | 1.32 | applied (compiled) |
| DT-1 | 15.9 -> **5.5** | 0.00 | 0.34 | 0.17 | 0.48 | applied (compiled) |
| Metal Zone | 27.2 -> **6.7** | 0.12 | 0.94 | 0.37 | 1.58 | applied (compiled); the envelope is the recovery after a rail-pinned Dist stage, against **8.97 dB for the reducedOrder model that shipped before** |
| OCD | 12.0 -> **6.8** | 0.27 | 0.20 | 0.26 | 1.47 | applied (compiled, batch 3) |
| ODR-1 | 11.1 -> **7.7** | 0.04 | 0.26 | 0.07 | 0.09 | applied (compiled, batch 3) |
| CrunchBox | 10.1 (unchanged) | 0.01 | 0.03 | 0.01 | 3.1 | compiled (x2.6) but NOT applied: one 5 ms frame at -33 dB off by 3 dB; every other metric now matches (the 7 dB peaks before were the netlist, see below) |
| Tone Bender (ge / si) | 8.9 (unchanged) | - | - | - | - | NOT applied: diverges with linear transistors; with `transistorFull` stable but off by up to 23 dB (ge), broken on the silicon model's one-port transistors, and slower than the netlist (x0.7) |
| SansAmp BDDI | 6.9 -> **2.7** | 0.19 | 0.21 | 0.22 | 0.36 | applied (compiled, batch 4: five blocks, the last with two inputs) |
| RAT / RAT 2 / Turbo Rat | 6.4-6.6 -> **3.0** | 0.01 | 0.41 | 0.03 | 1.11 | applied (2026-10-19: railsFirst + integrator clamp + the netlist fixes below) |
| DS-1 | 6.4 (unchanged) | 0.5 | 1.6 | 0.22 | 31 | compiled but NOT applied: the transistor gain stage (linearised in the compiler) behaves differently on hot bursts |
| Tube Driver | 10.1 (unchanged) | 0.11 | 1.4 | 6.8 | 1.6 | compiled (triode ports) but NOT applied: slower than the netlist (x0.55). Its peak error is the netlist's own convergence defect on triode circuits, which the fix below does not cover yet |
| HM-2 | 13.4 (unchanged) | 0.14 | 1.22 | 0.16 | 2.29 | NOT applied: matched only with the full two-junction transistor port (`transistorFull`) at 6 Newton steps, which is slower than the netlist (x0.55); fewer steps lose the saturation. No gain, netlist kept |
| Darkglass B7K | 11.6 -> **5.0** | 0.00 | 0.05 | 0.00 | 0.20 | applied (2026-10-19: railsFirst + integrator clamp + the netlist fixes below). Re-run 2026-10-20 after #117 changed its netlist (2.2K feed + 0.15 uF Grunt): still passes, same cost |

All applied ones also pass the steady-state test (`OversampledEffectTests`: DT-1 -115 dB, Metal Zone -128 dB
non-periodic error) and stay within 0.1 dB of unity with their existing trims (`PedalUnityLevelTests`).

Still above 5 %: the Metal Zone (five op-amp stages, every one of them clips, so all of it must stay oversampled) and the
DT-1 just over. The remaining cost is mostly the per-port solves of the stages pinned on a rail.

## Three fixes that closed the hot-burst gap (2026-10-19)

1. **Same integration rule as the netlist.** NodalCircuit integrates capacitors with the theta-method at theta = 0.6
   (`defaultTheta`, damps the top octave); the compiler used the trapezoid (0.5). Every hard edge differed by a sample.
   `LinearPortNetwork::setTheta` now takes the netlist's theta (`Description::theta`): DT-1 and ODR-1 match the netlist
   to 0.00 dB on every metric, the SansAmp envelope went 0.36 -> 0.06 dB.
2. **Op-amp macro integrator clamp** (`CompileOptions::integratorClamp`, per pedal). The netlist clamps the macro's
   integrator node with two diodes ~0.5 V past the rails, so it leaves a rail at once; the compiler had replaced them with
   a 470-ohm bleed into a 1-ohm source, which let the integrator wind up tens of volts and take ~100 us to come back.
   Now the follower stays an exact row and the integrator node gets the rail clamp. On for the Metal Zone, DT-1, ODR-1,
   SansAmp, RAT, Darkglass (and the measured-only CrunchBox / DS-1 / Tube Driver); off for the OCD (envelope 0.90 dB
   without, 1.65 with).
3. **A convergence defect in NodalCircuit itself.** Newton accepted a point when the linearisation leftover was below
   1e-5 V, but after a step the p-n junction log-limiter had cut, that leftover no longer measures the error, and the only
   other guard was a 30 V port residual. The RAT's silicon clipper node was accepted at 1.88 V (1.25 V off balance) on
   polarity flips: a one-sample spike in the netlist, the "2-5 dB peak" the RAT failed on, and the CrunchBox's 7 dB.
   Such points must now balance to 50 mV (`limitedResidualAccept`), on circuits without tubes (the tube amps' solver
   budgets were tuned around the old rule). This changes the NETLIST of every diode pedal on hard edges, toward the
   correct answer; all pedal suites pass.

The other suites were checked like for like (the same filter with the fix on and off): the amp failures a full local run
shows (AmpFullVsReducedConsistency, AC30 / Astroverb / 5150 / ENGL, the JCM800 and 5150 unity levels) are identical
without the fix -- they predate it and the CI smoke subset does not run them. The HM-2's measured-only figure depends
on which suites ran before it in the same process (1.88 dB alone, 41 dB after the pedal suites): shared state to find.

## Rule: a netlist change re-runs its equivalence

A compiled pedal is only as right as the netlist it was compiled from, and the compile happens at runtime from that
netlist, so a circuit fix (a new resistor, a changed cap) flows into the pipeline automatically -- but the acceptance
does not. Any PR that touches the netlist of a pedal with `pipelineEngine = true` re-runs its `CompiledPipeline` test
and the bench before merging (first case: #117, the B7K's Grunt network).

## Hot-burst envelope mismatches: investigation (2026-10-18)

RAT, DS-1, CrunchBox and Darkglass stay on the netlist. What was checked:

- **CrunchBox**: block by block (`pipeline::compareBlock`, gated bursts, now with a tail-energy metric) the compiled
  blocks match the netlist within 0.6 dB, tails included. The whole-pedal 8 dB envelope figure comes from a single
  5 ms frame at about -32 dB, close to the -40 dB floor of the envelope window, plus one-sample excursions of the
  netlist itself (the LED clipper node reaches 2.27 V for one sample where the pipeline holds 1.82 V). No compiler
  defect was found.
- **Darkglass**: the earlier 11 dB peak gap was the compiler's port order (fixed by `railsFirst`); what remains is
  2.8 dB of envelope at the recovery after the drive stage.
- **RAT / DS-1 / Tube Driver**: the mismatches appear only at high gain, when an op-amp stage pins its rail. The op-amp
  macro becomes a 1 ohm source + rail clamp + 470 ohm bleed in the compiler, so the recovery from the rail is
  where both models differ. More Gauss-Seidel sweeps (3) did not help and removed the speed-up.

So the open item is the op-amp rail recovery, not the diode / transistor ports. Until it is resolved, those pedals keep
`pipelineEngine = false` and their tests are "measured only".

## Triodes in the compiler (2026-10-18)

`NodalCircuit::describe()` now exports triodes and `CompiledCircuit` turns each one into a two-port device
(`TriodeDevice`, `LinearPortNetwork::addTriode`): grid-cathode and plate-cathode voltages, the same Koren model as the
netlist, linearised at the DC point, a 2x2 Newton with step limits (0.25 V grid, 25 V plate), 2 iterations. Pentodes are
not exported yet, so power stages cannot be compiled. On the Tube Driver it matches the netlist at low and medium Drive
(< 0.4 dB) but is not faster: a Koren evaluation (pow / exp / log) costs the same in both, so there is nothing to save
when the triodes are the expensive part. It pays off only where the linear part dominates (big tone stacks, many
passive nodes per tube), which is the case to measure on the amps.

## Next candidates (drive bench, 2026-10-15)

Over budget, worst first: Metal Zone 27 %, HM-2 16 %, DT-1 16 %, Bass Big Muff 14 %, Darkglass B7K 13 %, OCD 12 %,
Big Muff 12 % (done), ODR-1 11 %, Tube Driver 10 %, CrunchBox 10 %, then Tone Bender, SansAmp, the Rats, DS-1, BD-2,
Fuzz Face, Blues Breaker, Guvnor (5-9 %). The op-amp pedals need a linear op-amp macro plus rail-limit ports, and
their buffered op-amp outputs are natural cut points, so principle 5 (per-stage processing, oversampling only the
clipping stages) applies to them where it did not to the Big Muff.
