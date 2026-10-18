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
| CrunchBox | 10.1 (unchanged) | 0.07 | 1.32 | 7.1 | 8.3 | compiled (x2.4) but NOT applied: hot-burst peaks / envelopes off by 7-8 dB, the Darkglass symptom; not explained yet |
| Tone Bender (ge / si) | 8.9 (unchanged) | - | - | - | - | NOT applied: diverges with linear transistors; with `transistorFull` stable but off by up to 23 dB (ge), broken on the silicon model's one-port transistors, and slower than the netlist (x0.7) |
| Tube Driver | 10.1 (unchanged) | - | - | - | - | NOT converted: triodes, which the compiler does not express |
| HM-2 | 13.4 (unchanged) | 0.14 | 1.22 | 0.16 | 2.29 | NOT applied: matched only with the full two-junction transistor port (`transistorFull`) at 6 Newton steps, which is slower than the netlist (x0.55); fewer steps lose the saturation. No gain, netlist kept |
| Darkglass B7K | 11.6 (unchanged) | 0.06 | 1.43 | 11.3 | 7.0 | compiled (inductors + two-input EQ block added for it, x1.7) but NOT applied: the peak/envelope misses are the NETLIST's -- on hot bursts it throws one-sample 2 V spikes at the clipper because `NodalCircuit` caps the diode exponential at e^40 (~0.6 A). Fix that in NodalCircuit, re-verify, then switch |

All applied ones also pass the steady-state test (`OversampledEffectTests`: DT-1 -115 dB, Metal Zone -128 dB
non-periodic error) and stay within 0.1 dB of unity with their existing trims (`PedalUnityLevelTests`).

Still above 5 %: the Metal Zone (five op-amp stages, every one of them clips, so all of it must stay oversampled) and the
DT-1 just over. The remaining cost is mostly the per-port solves of the stages pinned on a rail.

## Next candidates (drive bench, 2026-10-15)

Over budget, worst first: Metal Zone 27 %, HM-2 16 %, DT-1 16 %, Bass Big Muff 14 %, Darkglass B7K 13 %, OCD 12 %,
Big Muff 12 % (done), ODR-1 11 %, Tube Driver 10 %, CrunchBox 10 %, then Tone Bender, SansAmp, the Rats, DS-1, BD-2,
Fuzz Face, Blues Breaker, Guvnor (5-9 %). The op-amp pedals need a linear op-amp macro plus rail-limit ports, and
their buffered op-amp outputs are natural cut points, so principle 5 (per-stage processing, oversampling only the
clipping stages) applies to them where it did not to the Big Muff.
