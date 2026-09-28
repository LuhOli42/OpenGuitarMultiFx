# NodalCircuit — the netlist solver

`Source/Effects/NodalCircuit.h`. A small SPICE-style solver (modified nodal
analysis, trapezoidal capacitors, Newton-Raphson for nonlinear devices),
sized for one stompbox stage at audio rate. It exists because the pedals it
runs are **not chains**: the Klon Centaur's two feed-forward networks (one
referenced through the second gang of the Gain pot) and summing amp all
interact; the BD-2's gain stages are discrete JFET/PNP op-amps with the Gain
pot in their feedback; the HM-2's tone section is three gyrators hung on two
pots. The earlier pedals here were solved by hand-deriving a Thevenin
reduction per stage, which stops being trustworthy once stages interact.
The TS808/9/10 and OD-1 processors are still hand-derived; the DS-1 was ported
to a netlist (its hand-derived version coupled stages one sample late, which
made it chaotic at high gain -- non-periodic error -5 dB, now -322 dB).

## Elements
Resistors, capacitors (trapezoidal companion, same as `TrapezoidalCapacitor`),
driven nodes (rails, the input), **ideal op-amps** (output row replaced by the
virtual-short constraint, so no extra unknown), Shockley diodes, BJTs and
N-channel JFETs. The BJT and JFET equations are `EbersMollBJT::evaluate()` and
`ShichmanHodgesJFET::evaluate()` — one home for each device model, shared
with the older processors. Since 2026-09-21: **triodes and beam tetrodes** (`addTriode`, `addPentode`,
`TubeModels.h`), **coupled inductors** (`addCoupledInductors`) and **current sources**
(`addCurrentSource`); see "Tubes, transformers and supplies" below.

## How a sample is solved (the "DK method", state-space form since 2026-09-20)
Everything linear is folded, once per change of a resistance, into dense maps from an
*excitation* vector E = [capacitor history currents, source voltages, 1] to the port
voltages (u0 = Hu E), the capacitor voltages (Hv E - Kv i) and, on demand, any node voltage
(Hx E - W^T i). A sample is then: build E, u0 = Hu E, Newton over the ports, advance the
capacitors -- no per-sample assembly, factorisation or back-substitution over the node set.
A knob turn costs one LU plus one back-substitution per excitation, every 16 samples. Each
saturating-op-amp state has its own map. `voltage(node)` reads the last accepted sample
(the DC solution until the first sample). Effect: the fixed cost of a purely linear block
dropped from ~450 to ~150 cycles; nonlinear blocks are dominated by Newton (below).

## How a sample was solved before (kept for the DC path)
The linear part is LU-factored once and again only when a resistance changes
(pots are updated at control rate, every 16 samples, smoothed). Each nonlinear
device exposes a few *ports* (a diode: 1 branch voltage; a BJT or JFET: 2).
Per sample the linear network is back-substituted once, then Newton-Raphson
runs **only over the port voltages** (a handful of unknowns), not the whole
matrix. The solution is identical to a full Newton solve.

Robustness work that turned out to matter (each was a real failure found by a
test, not a guess):

- **DC operating point by pseudo-transient relaxation** (backward-Euler steps
  of growing size from the initial guess), falling back to a static
  source-stepped Newton. A raw Newton on the static equations diverged on the
  BD-2's gain stage (high loop gain). It also means a circuit starts *settled*
  instead of needing seconds of silence like the older processors.
- **SPICE `pnjlim` junction limiting** on diode and BJT ports. Without it, a
  diode network behind a large resistance (the HM-2's clipping op-amp) needed a
  ~740 V no-device response to be pulled back to ~0.6 V: plain Newton overshoots
  the exponential and then sheds only ~Vt per iteration, failing 4-18% of
  samples. With it: 0.
- **Convergence is MEASURED, not estimated.** After each Newton step the devices are
  evaluated at the new point; the step solved the linearised equations exactly, so the
  whole leftover error is `cur_actual - (cur_before + D * step)`, and pushing that through
  `W` gives the error in every node voltage -- the thing the audio hears. The point is
  accepted when that is below 10 uV, and the currents evaluated AT it are used for the
  reconstruction (no stale currents). The previous rule (accept when the step's estimated
  node change was < 300 uV, linearised at the point the step *started* from) turned out
  to leave errors of 0.1-0.5 mV against a 1 nV reference (a diode turning on has slope ~0
  at the start, so a huge step "changes nothing"); this one agrees with the reference to
  a few uV, and the evaluation it needs is the one the next iteration would have done
  anyway (Newton iterations are 1.0-1.3 per sample in practice).
- **Newton warm start**: the previous port voltages extrapolated linearly by
  the last step, with junction ports passed through the same `pnjlim` limit.
  (Unlimited, the extrapolation threw a fast edge into exp() overflow and made
  the BD-2 fail 98% of its solves.) Cuts the average iterations per sample by
  ~10%.
- **`EbersMollBJT::solve()` uses `pnjlim` too** (damped so the vbe/vbc steps
  obey it) with 40 iterations instead of 12. Without it the Booster's and DS-1's
  transistors failed to converge on ~1-2% of samples; a failed solve holds the
  previous value, which is a click -- hiss at that rate. Now 0 failures at 1x.
- **Capacitors integrate with theta = 0.6, not the trapezoid's 0.5** (`i_eq =
  g v + (1-theta)/theta i`, `g = C fs / theta`). The trapezoid rule has no damping
  at Nyquist, so a stiff switching node (the HM-2's diodes, the DS-1's clipper)
  rings at fs/2 for a long time, which is the "harsh, artificial" top end.
  theta = 0.6 cuts that ringing energy above 12 kHz by ~9 dB and moves the audible
  band by 0.03 dB. `NodalCircuit::defaultTheta` / `setIntegrationTheta()`;
  the closed-form solver tests set 0.5 explicitly.
- **Never cut a block where the downstream gain is high** (see the HM-2 doc): a
  one-sample-delayed or predicted coupling is multiplied by whatever gain follows.
- Initial guesses matter for the relaxation: a guess that contradicts the DC
  point (4 V on a node the diodes hold at 0 V) hands the diodes a huge current.

## What was simplified to make it cheap (2026-09-20), and how each was checked
Same sound, fewer Newton dimensions. Every change below was compared against the
previous solver on 12 renders (6 pedals x 2 gain settings, a decaying three-note chord);
"timbre" = octave-band spectral shape with the level removed, worst band:

- **Antiparallel diodes are one device with one port** (`i = Is1(e^{v/n1}-1) - Is2(e^{-v/n2}-1)`):
  exact (-170 dB vs before).
- **A BJT whose collector is on a fixed rail is a one-port device** (vbe only; its
  collector junction is ~1e-15 A): exact.
- **A JFET marked `assumedVds` is a one-port device** (channel-length modulation taken at
  that vds; used for the BD-2's tail-pair devices with their drain on the rail): BD-2 timbre
  0.7 dB.
- **Emitter/source followers that only buffer are ideal followers** (`addFollower(in, out,
  drop)`: out = in - drop, no base current, no Newton port): DS-1 Q1/Q3, HM-2 Q1, BD-2
  Q3/Q7/Q1, TS Q1/Q2, OD-1 Q6/Q7. Timbre within 0.04-0.3 dB (BD-2 0.8), waveform ~-28 dB
  because a 1-5% gain change is amplified by the clipping stages after them (the pedals
  are re-trimmed to unity, so only the shape matters). Caveat learned in the OD-1: an
  ideal source straight across a capacitor makes the trapezoid rule ring at Nyquist
  forever, so the follower's output resistance (1/gm, 74 ohm) stays in that branch.
- **The DS-1's Q6 JFET is a 333 ohm resistor** (used as a VCR with vgs ~ 0 in the triode
  region; the signal there is < 0.3 V against Vp = -2 V): timbre 0.04 dB.

## Where the cycles go (BD-2, 1x, cycles per input sample; per block: E+u0 / Newton / caps)
| block | ports | cycles |
|---|---|---|
| A, D, E (linear) | 0 | 140-220 each |
| B (gain stage 1 + tone + clippers) | 6 | ~2100 (Newton 1600) |
| C (gain stage 2 + tone + level) | 5 | ~1800 (Newton 1300) |
Newton = initial evaluation ~150, J build ~300, LU ~350, limit/step ~140, verification
evaluation ~140, node error ~100 per block. No single hot spot; a step below 0.5 mV is
accepted without the verification evaluation (error <= 3 uV). Things that were tried and
did not move the number: fast exp (std::exp is 6.5 ns throughput / 16 ns latency, a
polynomial version is no faster), row-oriented J build and compile-time-sized LU,
fewer iterations (they are already 1.0-1.4 per sample). The budget for 5% of a core at 2x
is ~1500 cycles per oversampled sample for the WHOLE pedal, i.e. about one nonlinear block:
the BD-2 (two of them) and the HM-2 (three, one with five ports) cannot fit it at 2x with a
generic netlist solver, whatever is tuned; that takes a reduced-order model of their gain
stages (see docs/circuits/Oversampling.md).

## Saturating op-amps (`addSaturatingOpAmp`)
A real op-amp on 9 V runs out of output swing (TA7136AP: "+1.5 V to Vcc - 1.5 V";
M5218AL and TL072 similar) long before the diodes after it clip, which changes what
the coupling capacitors after it see and how the stage recovers. An ideal op-amp is a
constraint row (V+ == V-) with unlimited output; clamping that output afterwards is
wrong, because the feedback network would still be driven as if the loop were closed.

Modelled as a **three-state switch, solved exactly**: ideal (V+ == V-), held at the
high rail, held at the low rail. In a held state the constraint row is replaced by
`V(out) = rail`, so the inverting input really stops following the non-inverting one
and the capacitors in the feedback network keep charging. Each state has its own LU
(built lazily, parked when not active, dropped when a resistance changes); a sample
tries the previous state first and switches when the answer is inconsistent (ideal:
output outside the rails; held: (+)-(-) no longer pushing into the rail), at most
four passes. Inside the swing it is bit-for-bit the ideal op-amp; outside, the
output sits exactly on the rail (`NodalCircuitTests`). No extra Newton port, and
because a saturated stage no longer drives tens of volts into the clipping diodes the
DS-1 got *cheaper* (44% -> 23% of a core at 4x). At most one per circuit; every
block that needs one has exactly one.

Tried first and rejected: a finite-gain VCVS behind an output resistance with clamp
diodes (unbounded currents, 0.5 V overshoot beyond the rail), and a nonlinear VCCS with
a tanh-like knee (Newton overshoots across the knee when it starts saturated: a
2-cycle between +-0.3 V). Both are stiff because the device's slope is ~0 exactly where
Newton needs it; the switch has no such region.

Where it is used: DS-1 (TA7136AP, rails 1.5 / 7.5 V -- the one it really changes: the
stage is followed by a shunt clipper so its output swings to the rails), HM-2 IC3A
(M5218AL), BD-2 IC1B (M5218AL), Klon gain stage (TL072, +-7.5 V about the 4.5 V
reference). The last three never reach their rails with a normal guitar signal, so
they sound the same; they are there so they do when driven hard.

## Tubes, transformers and supplies (2026-09-21, for the Bassman amp)
* **Triode / pentode** = a two-port device like a BJT (ports vgk and vpk, currents ip and ig), so the whole
  DK machinery applies unchanged. The pentode's screen is not a node: `setPentodeScreen()` supplies its
  voltage per sample. `pentodeCurrents()` reads plate and screen current cheaply (the supply model needs them
  every sample). Models and fits: [`Bassman5F6A.md`](./Bassman5F6A.md).
* **Coupled inductors**: `addCoupledInductors(windings, L matrix)`. Each winding is one entry of the folded
  state exactly like a capacitor (the same E / Hv / Kv rows); the companion model is
  `i = G v + hist`, `G = theta/fs * L^-1`, `hist = i_prev + (1-theta)/theta * G v_prev`, and the stamp is the
  full 3x3 conductance matrix (cross terms included). During the DC relaxation a winding is a wire (a stiff
  1e4 S conductance, its current becomes the initial state): backward-Euler pseudo-transient on an inductor
  makes it an open circuit for the first steps and the tubes then see a supply that is not there yet.
* **Current sources** (`addCurrentSource`): an entry of E like a voltage source, injecting into a node that stays
  an unknown. Used by the supply model for the tubes' current draw.
* `maxUnknowns` is now 32 (was 24); ports still 16.
* **Newton safeguards for tube circuits.** (1) Per-port step limits: 1 V for junctions as before, 10 V for a tube
  grid, 40 V for a plate (a power stage moves tens of volts per sample; with the 1 V limit it needed 13+ iterations
  and failed). (2) A backtracking line search on the port-equation residual, **only when the block has tubes** (a
  diode's exponential makes the residual rise before it falls; enabling it for the pedals cost them accuracy --
  the OversampledEffect "steady state" metric went from -155 dB to -15 dB and the HM-2's noon gain moved 0.6 dB).
  (3) If Newton fails from the predicted start, retry from the last converged point without extrapolation, then
  with steps a quarter as long. (4) The linear predictor stays: a second-order predictor cut cost 20% but a hard
  attack sent Newton to a spurious operating point (output died), see the doc's cost section.
* **DC operating point of a big block needs consistent starting guesses.** The Bassman's tone stack is DC-coupled
  to the cathode follower's 189 V; starting the relaxation with the capacitors "charged" to 0 V made the first
  step a 189 V jump that put the phase inverter's grid at +220 V and the relaxation never recovered (only when the
  treble pot happened to be at 1 ohm; other positions were slow enough to survive). `setInitialGuess` on the two nodes
  the follower reaches (the DC path stops at the capacitors) fixed it; `BassmanStyleAmplifierProcessorTests`
  runs all 32 knob corners.

## Coupled junctions: one limiter fraction (2026-09-21)
Found through the Guv'nor "dying" report. Two junction ports whose voltages are tied by the network (the two rail-clamp diodes of
`addOpAmpMacro`: u_a + u_b is a constant of the circuit) were limited independently: the forward one by SPICE's `pnjlim` (a huge
step compressed to a fraction of a volt), the reverse one by its own reverse limit (~2|v|, growing every iteration). Then the
uniform step scaling for the port limit shrank the *forward* port's step by the reverse port's ratio, and the solve crawled
0.003 V per iteration for 300 iterations (3 modes x 100), failed, and -- because a failed solve leaves the state untouched --
froze the circuit: constant output (the sound "dies") and 300 Newton iterations per sample (the CPU spikes to ~40 us/sample, 60x
the normal cost). A reverse-moving junction now moves by the same fraction the forward limiter allowed
(`forwardFraction`), which keeps u_a + u_b conserved. Random knob moves + plucked notes + x6 bursts through the Guv'nor:
31-94% failed solves before, 0 after (`Tests/PedalStress.h`, run by every op-amp pedal's tests); the probe's runtime went from
42 s to 1.6 s. The whole suite (Bassman, HM-2, BD-2 included) is unchanged.

## The fallback modes are capped in a runaway (2026-09-21)
`newtonPorts()` tries the predicted start, then the last converged point, then that with quarter steps -- 3 x 100 iterations
for a sample that fails all three. Those fallbacks exist to rescue an *occasional* bad sample; once several samples in a row
have failed the block is in a runaway and they rescue nothing, they just burn ~450 us per sample **on the audio thread,
exactly while the circuit is being driven hardest**. Measured with a distortion pedal at full gain into the Bassman: a burst
of failing samples made one 128-sample block cost **13.0 ms against a 2.67 ms budget** -- an audible dropout, and a CPU meter
reading ~500%. After two consecutive failures the fallbacks are skipped (`consecutiveFailures`), which took that block to
5.8 ms; the caller's own recovery (the Bassman's, at 8 consecutive failures instead of 48) took it to **2.19 ms, inside
budget**. Note what did NOT help: relaxing `defaultNodeTolerance` from 1e-5 to 1e-3 V left the number of slow solves
unchanged (~28k in a 20 s run either way) and made failures **more** frequent -- the slow solves are limited by the Newton
step limiters (junction `pnjlim`, the per-port volt limit), not by the tolerance they are grinding toward.

## Tube model gradient dead zone, and an evidence-gated iteration budget (2026-09-22)
Chasing "still bugging" at the most extreme Bassman drive (`Bassman5F6A.md`), a captured failure trace (`nv=8`, the
power block) showed Newton creeping toward its target at a small, FIXED volts/iteration -- correctly directed, just too
slow for the 100-iteration budget when the forcing had moved by hundreds of volts in one sample. Two real fixes, both
general (not Bassman-specific):

1. **`KorenTriode::evaluate()`, `KorenPentode::evaluate()` and `GridCurrent::evaluate()` had an asymmetric dead zone**
   (`TubeModels.h`): the table's HIGH edge already fell back to the exact analytic SoftPlus formula (smooth arbitrarily
   far into that tail); the LOW edge (deep grid cutoff) instead returned current AND derivative as a hard, literal
   zero. A device with zero local gradient gives Newton no information to steer by -- a real dead zone, not just a
   small modelling error, and exactly the kind of thing a hard-driven grid can reach. Fixed by using the SAME SoftPlus
   fallback both directions (SoftPlus is smooth, non-zero, down to machine-epsilon underflow, never a hard floor).
   Benefits every tube circuit that ever drives a grid hard, not just this one Bassman corner.
2. **Mode 2's (the last-resort quarter-step fallback) iteration ceiling is now evidence-gated, not fixed at 100**: past
   100 iterations, every 100 more checks whether the pre-limit Newton step has shrunk by at least 2%; if it has, it
   keeps going (up to a 900-iteration safety ceiling); if it hasn't, it gives up exactly as before. A normal sample
   converges in single digits of iterations and never reaches the check, so this costs nothing for any other circuit,
   present or future. Mode 2 is now also tried even during a consecutive-failure streak (mode 1 still isn't -- see the
   comment on `newtonPorts()`), since its own cost is now self-limiting.

Together: the realistic single-channel extreme case (Bassman Input = Normal, every preamp control maxed) went from
~15-20 recoveries in 30 s to ~6-9. The most extreme corner (Input = Jumped, doubling the drive, still with everything
maxed) is NOT fixed: a trace showed Newton making genuine, sustained progress for several hundred iterations and then
the proposed step spiking by orders of magnitude at one specific iteration (the actual, limited port voltages stayed
physically reasonable throughout -- a transient ill-conditioned Jacobian, not a state runaway). Left as an open,
now precisely characterized problem rather than tuned further under time pressure -- see `Bassman5F6A.md`.

## The near-singular-Jacobian corner, closed at its actual source, not patched around (2026-09-22)
Continuing the item directly above, per the user's explicit ask ("uma solução aplicavel pra tudo... n sobrecarregue a
cpu... mantenha a dinamica"). A targeted trace (temporary, removed after) caught the exact mechanism: at iteration
~530-600 of the extended mode-2 solve, two specific ports (a triode's `vpk`, a pentode's `vgk`) had a raw, pre-limit
proposed Newton step of ~11,000-12,000 V while their own Jacobian diagonal entries had collapsed toward zero (a real,
if transient, local near-singularity), and every other port's own proposal stayed under ~600 V. Because the step
limiter's shared scale factor is computed from the SINGLE WORST port and applied UNIFORMLY (deliberately -- see
"Coupled junctions" above, it's what keeps a Newton step a genuine descent direction), those two bad ports crushed
progress for all 8 ports together every iteration -- the actual mechanism behind "creeping forever, never arriving".

**Two solver-level fixes were tried and BOTH reverted** -- worth recording so a future pass doesn't retry them:
1. **Independent per-port step clamping** (each port capped to its own limit, no shared scale): a severe regression.
   The extreme case's recoveries went 167 -> 1943 (in a differently-sized run), and unrelated PREAMP failures (a block
   this change has no business touching) exploded too, proving it breaks Newton's descent-direction guarantee broadly,
   not just at the target corner.
2. **Levenberg-Marquardt diagonal damping** (add a small lambda to the Jacobian's diagonal and re-solve, only when the
   raw step implied was >500x a port's physical limit): safer in shape (still one shared linear solve, just
   regularised) but STILL regressed the realistic case even when gated tightly to "mode 2, past iteration 200" --
   recoveries in `BassmanHotInputProbe`/`BASSMAN_INPUT=0` went 8 -> 15 (measured with the fix's own gate active, vs 8
   with the code fully disabled -- confirmed by literally toggling the gate off and rebuilding, not inferred). An
   ordinary, otherwise-converging mode-2 sample can transiently pass through a large-implied-step point on its way to
   the right answer; damping the linear system there changes which direction the step takes and knocks some of those
   off their working trajectory. Extreme-case recoveries only fell 167 -> 134 in exchange -- a bad trade given the
   regression, and abandoned.

**What actually worked: the near-singularity's real source, not the Newton solve.** `KorenTriode::evaluate()` /
`evaluateExact()` and `KorenPentode::evaluate()` all had `if (vpk <= 0.0) return o;` -- current AND both derivatives
hard-zeroed the moment a plate voltage reaches or crosses its own cathode. This is the EXACT SAME asymmetric dead-zone
bug already found and fixed on the grid side above, just on the plate side, and never touched by that pass. A device
sitting in this dead zone contributes nothing to the Jacobian's diagonal for that port -- literally the near-zero
diagonal entry the trace caught. `TubeModels.h`'s `floorPlateVoltage()` replaces the hard floor with a smooth SoftPlus
extension (`vf = scale * softplus(vpk/scale)`, `scale = 2 V`): always positive, always has a real (if tiny) gradient,
and is numerically IDENTICAL to the raw formula (bit-for-bit, since SoftPlus's own `x>30` fast path returns its input
unchanged) for any plate more than ~60 V positive -- normal tube operation never comes near that band, so this is free
everywhere except the exact deep-cutoff/reverse-plate region Newton was getting stuck in.

**Result, measured with the full regression suite passing (not just the target scenario)**:
`BassmanHotInputProbe`, `BASSMAN_KNOBS_MAX=1`, `HM2StyleDistortion` in front, 30 s:
- Input = Normal (the realistic case): **8 -> 0 recoveries.** Fully clean.
- Input = Jumped, every control maxed (the most extreme corner): **167 -> 35 recoveries**, a ~4.8x reduction. `pre 0,
  power 133` -- everything remaining is in the power block, none in the preamp, consistent with the original trace.
Not fully zero at the most extreme corner; the honest remaining gap is smaller now and could have another instance of
the same class of bug in it (the pentode's `atan`/knee table, or the flash-over onset), but wasn't chased further
after two solver-level attempts already showed that guessing at the linear algebra risks regressing far more than it
fixes -- the next step, if resumed, is the same trace-driven approach that found this one, not another threshold.

## Why splitting a block (Gauss-Seidel) does NOT make it cheaper -- measured, 2026-09-22
The backlog carried "block-Gauss-Seidel between phase inverter and output stage" as the big remaining lever for the
Bassman's cost. Measured before building it, and the premise does not hold:

* **Per-iteration cost barely depends on how many ports the block has.** The Bassman's own two blocks, side by side:
  preamp nv=6, 1.52 iterations/sample, ~3300 cycles => **~2000 cycles per iteration**; power nv=8, 2.99
  iterations/sample, ~6500 cycles => **~2100 cycles per iteration**. Two more ports cost ~5% per iteration, not the
  ~2.4x an O(nv^3) dense solve would imply.
* That matches the profile already in this file: device evaluation 38%, the dense port solve 23%, Jacobian/error
  bookkeeping 38%. **Only the 23% scales with block size.** Splitting nv=8 into two nv=4 blocks cuts part of that
  23%, but multiplies the device evaluations AND the bookkeeping by the number of outer sweeps. At two sweeps it is
  already a net loss; Gauss-Seidel needs at least two, and this particular boundary is crossed by the global
  feedback loop (speaker -> 27k -> presence -> phase-inverter grid), the coupling that makes such splits converge
  slowest.
* The `NodalCircuit` surface needed for it (`resolveWithoutCommitting()` / `commitAfterGaussSeidel()`, a solve that
  does not advance capacitor history or age the inter-sample predictor) was written and then **removed again** once
  the above was measured -- no point carrying engine surface for a technique the numbers say is slower. One thing
  worth keeping from the attempt: moving the predictor ageing out of `newtonPortsFrom()` (where it has always lived)
  broke convergence badly in circuits under knob motion (`GuvnorStyleDistortionProcessorTests`' knob-motion stress
  went to an 83% solve-failure rate). It is load-bearing exactly where it is.

## What the convergence tolerance is worth -- measured, 2026-09-22
`nodeTolerance` is 10 uV of leftover node error. On a power stage swinging +-400 V that is ~25 parts per billion, so
loosening it looks like free CPU. It is not: `BassmanToleranceSweep` (dev-only, `BASSMAN_TOL_SWEEP=1`, in
`Tests/PedalDeathProbe.cpp`) renders one HM-2-at-full-gain signal through one amp per tolerance:

| nodeTolerance | cost | preamp iters | power iters |
|---|---|---|---|
| 10 uV (default) | 25.4% | 1.52 | 2.99 |
| 100 uV | 25.0% | 1.37 | 2.79 |
| 1 mV | 24.1% | 1.25 | 2.63 |
| 5 mV | 23.4% | 1.18 | 2.54 |
| 20 mV | 22.9% | 1.10 | 2.47 |

**2000x looser buys 10%.** Iterations have a floor around 2.5 for this block -- the work is the first iteration plus
the verifying evaluation, not a long tail of refinement, so there is no cheap accuracy/cost trade here. (The sweep
also prints a sample-wise difference against the 10 uV render; at full gain the amp is chaotic, so that number
measures trajectory divergence, not audible error -- judge any such change on octave-band shape, as the reduced-order
work does, never on the waveform difference.)

## Macro-model elements (2026-09-21)
* `addFiniteGainOpAmp(plus, minus, out, gain, offset, commonMode)`: an op-amp row `(1+cm) V+ - (1-cm) V- - V(out)/gain =
  offset`; no extra unknown. With an RC after it and an `addSaturatingOpAmp` follower it is the macro-model of a discrete
  op-amp (finite DC gain, dominant pole, output swing, finite CMRR): used for the BD-2's two gain stages.
* `addOpAmpMacro(plus, minus, out, OpAmpMacro{dcGain, gainBandwidth, outputOhms, lowRail, highRail, commonMode, offset})`
  (2026-09-21): the recipe above as one call for a real single-supply op-amp with a datasheet GBW (finite gain -> 1M*C
  pole at GBW/A0 -> a diode from the pole node to each rail -> saturating follower -> output resistance), ONE per block (the
  saturating follower). Used by the 741 of the Distortion+ and DOD 250, the LM308 of the RAT and the TL072s of the Guv'nor and
  Blues Breaker (each checked against its closed form to 0.1-0.5 dB). **The rail diodes are not optional**: an op-amp with an
  open-loop gain of 2e5 and a 5 Hz pole winds its integrator up while the output sits on a rail (~1 V of error x 2e5) and
  unwinds late -- 560 us of delay at 440 Hz without them, 23 us with them (`OpAmpClipperDistortionProcessorTests`, "hard
  clipping does not delay the op-amp"). The 1M (not 1K) integrator resistor keeps the clamp current at ~0.1 A instead of
  ~100 A, which is what let the Newton solve converge (0.8% failed solves with 1K). Costs two Newton ports (the BD-2's
  macro is not affected: its pole is at 11 kHz, its integrator cannot wind up). No slew limit, no input-pair limiting: see
  `RatStyleDistortion.md` for where that matters.
* `addBjtSaturating(c, b, e, pnp, params, iSat)`: a one-port BJT (vbe only) whose collector current limits smoothly at
  iSat, `ic / (1 + (ic/iSat)^4)^(1/4)`; used for the HM-2's Q6/Q7. Base current stays iC/beta of the unsaturated current.
* Both are checked against the full netlists by `Tests/ReducedOrderEquivalenceTests.cpp`; a reduced model is only
  accepted when level, octave-band shape (bands above -30 dB) and small-signal response match (docs of each pedal).
* Tried and rejected for the tube blocks: a second-order (quadratic) port predictor with a trajectory-smoothness guard
  (no cost gain at the safe thresholds; unstable at the unsafe ones), rejecting unphysical tube-port roots, and continuation on
  the no-device port voltages (neither changed the failures it was meant to fix: the cause was the circuit, see
  `Bassman5F6A.md`); `-march=native -fno-math-errno` for the circuit processors (no gain).

## Verification (Tests/NodalCircuitTests.cpp)
RC low-pass, ideal op-amp with a series-cap gain leg, and a diode clipper match
closed-form/independent scalar solves to 5 digits; NPN, PNP and JFET DC points
match `EbersMollBJT::solve()` / `ShichmanHodgesJFET::solve()`; and the strongest
check: **a 21-unknown TS808 netlist reproduces the hand-derived
`TubeScreamerStyleOverdriveProcessor` to 0.013% / 0.002%** (two independent
methods, same circuit).

## Cost
| | unknowns | us/sample (1 channel) | % of one core @ 48 kHz |
|---|---|---|---|
| TS808 netlist (test) | 21 | ~0.45 | 2.2% |
| Centaur | 2 + 16 + 7 | ~0.5 | 2.4% |
| DS-1 | 2 blocks | ~0.7 | 3.5% |
| HM-2 | 3 blocks | ~1.6 | 7.8% |
| BD-2 | 5 blocks | ~1.7 | 8.1% |

(All at the host rate. The pedals with a clipper run through
[`OversampledEffect`](./Oversampling.md), which multiplies these by 2 or 4.)

Stereo doubles it unless the block is **dual-mono** (a mono guitar fed to both
channels): the processors then solve one channel and copy the output.
Identical input is not enough (the circuits' capacitor *states* must be
identical too), so the shortcut is used only while the channels are known to be
in sync; it switches off after any divergence and re-arms after 10 s of
identical input. Tested exact (0.000000000 error) across dual/different/dual.

## Limits (shared by every processor built on it)
- **Op-amps are ideal inside their swing**: no gain-bandwidth (parts in the MHz
  range: M5218AL 7 MHz, 4558 ~3 MHz, TL072 3 MHz -- closed-loop bandwidth GBW/gain stays
  above the audio band; the TA7136AP's GBW is not in its datasheet, assumed similar),
  slew rate (TA7136AP 0.5 V/us, M5218AL 3 V/us: only the edges of a hard square
  wave) or input protection. Output rail clipping IS modelled where it matters, see below.
  Discrete transistor stages saturate anyway (they are real transistors in
  the model).
- Gate current of a JFET is zero; BJT/diode series resistance and junction
  capacitances are not modelled.
- Blocks are cut where an ideal op-amp output / JFET gate / op-amp (+) pin
  cannot be loaded back. Where that isn't exact, the approximation is stated in
  the pedal's own doc (none currently: the HM-2's predicted node was removed by merging its op-amp into block 1).
- `maxUnknowns = 32`, at most 16 device ports per block.

Define `NODAL_DEBUG` to trace Newton iterations to stdout (never in the audio path).

## A "Fast" power stage for the Super Lead: measured, and it does not pay (2026-09-26)
Proposed to cut the Super Lead's ~24% of a core: (1) fewer Newton iterations per sample, (2) the tone stack out of the Newton block, (3) the push-pull pair collapsed to one differential element.
Built (1) as a per-instance solver profile behind a Full / Fast switch and swept it against the full solve (`SL_AB` in the Super Lead tests, since removed): **no profile saved anything**.
| profile | power iterations/sample | cost | difference from the full solve |
|---|---|---|---|
| default | 2.00 / 2.04 / 2.05 | 20.2 / 22.1 / 22.5% | reference |
| small-step accept 5 mV, caps 25/30/250 | 2.00 / 2.04 / 2.41 | same (Jumped hot: 3% MORE) | -37.9 / -25.5 / -17.6 dB (audible-ish, and worse when hot) |
| + quadratic predictor 0.3 | 1.96 / 2.05 / 2.11 | same | -36.0 / -23.3 / -20.6 dB |
| linear predictor 1.1 | 2.19 / 2.41 / 2.79 | worse | -34.9 / -25.1 / -17.9 dB |
The floor is 2 iterations per sample (one full step with its verification evaluation, one Jacobian/LU to confirm) at ~2000 cycles each; a looser acceptance changes the
result (the PI's gain of ~100 turns a 2 mV step into an error you can measure) without removing an iteration. (2) measured by replacing the tone stack with two resistors:
20.7% against 22.0% at low level, no difference when hot. (3) would remove two of eight ports, and per-iteration cost barely depends on ports (5% for two, see above).
Together at most ~10% for a change of sound, so none of it was shipped and the switch was removed. The levers that DO move the number are the ones already listed here (the
tolerance sweep, ~5-10%) and structural ones: half-rate preamp, sleeping when the input is silent, a reduced-order behavioural power stage.

## A wall-clock real-time guard: tried, and removed (2026-09-27)
Motivated by the same CPU-spike concern as above, but from the other direction: instead of making the average solve cheaper, bound the WORST case by watching the wall clock per block and
falling back to a hard, fixed Newton-iteration cap for whatever is left of that block. Built for the Super Lead (`deadlineFraction`/`deadlineIterations`, checked every 8 samples), and removed
the same day it was measured properly:
* The trigger threshold (55% of the block's own real-time budget) was picked assuming normal playing tops out around ~50%. It doesn't: the worst *reference* block (nothing forced) already costs
  37-68% depending on how hard the amp is driven, so the guard fired on ordinary hot playing, not just a genuine emergency.
* Once it fires, a fixed 3-iteration cap applied to every sample for the rest of the block is not a mild quality trade -- it is the same bounded-Newton experiment this doc's own earlier
  `SL_BUDGET` sweep already condemned (a forced deadline mode gives 4-19% failed samples and thousands of recoveries): it degrades samples that would have converged fine in 1-2 iterations, just
  as much as the rare sample that actually needed the help. Confirmed directly: the Super Lead's "hot pedal into ONE channel" regression test went from 0 recoveries to 500-836 with the guard
  active, and back to 0 with it removed and nothing else changed.
* **Lesson for any future per-block "degrade the rest of the block" idea**: a block-wide quality cliff punishes every sample in that block for the sins of the one or two that are actually
  expensive. A per-SAMPLE ceiling (see next section) is the finer-grained, evidence-based alternative -- it only costs quality on the specific sample that needed more than the ceiling allows.

## `NodalCircuit`'s mode-2 iteration ceiling: 900 -> 300, evidence-based (2026-09-27)
Mode 2 (the last-resort quarter-step Newton fallback, shared by every processor on this solver) had a 900-iteration/sample safety ceiling with an evidence-gated stall check (give up early if the
step stops shrinking). Measured directly (`SL_ITERHIST` in the Super Lead tests: a histogram of `NodalCircuit::lastIterations()` over 4 s / 192 000 samples of a hot-pedal-into-the-amp signal,
picked because it's what reaches the highest iteration counts of anything tried so far):

| bucket (iterations) | <5 | 5-9 | 10-19 | 20-29 | 30-49 | 50-99 | 100-199 | >=200 |
|---|---|---|---|---|---|---|---|---|
| samples (of 192 000) | 183 114 | 4 808 | 859 | 2 655 | 392 | 136 | 5 | 31 |

So the overwhelming majority never come close to the ceiling, but a real, non-zero tail does. Cut to 300 (evidence: **zero regressions across the full test suite**, every processor, not just
this amp) -- a genuine 3x reduction in the absolute worst-case cost any single sample can impose. A further cut to 100 was tried and reverted: it turned 0 recoveries into 84 on the Super Lead's
own "hot pedal into ONE channel / Bright" test, i.e. some of that tail genuinely needs more than 100 iterations to reach a sane answer, and capping it there traded real correctness for a bound
this solver doesn't need. **300 is the number verified safe; don't lower it again without the same kind of full-suite evidence, and don't raise it back toward 900 without a documented reason.**

This bounds the absolute worst SINGLE SAMPLE, but does not bound a whole BLOCK: a block containing several samples each costing, say, 50-100 iterations still adds up, and that was the source of the
Super Lead's own remaining worst-block spikes. **Superseded for the Super Lead** by a reduced-order (behavioural) power stage that removes the Newton solve from the power section entirely --
see `docs/circuits/SuperLead1959.md`'s "Reduced-order (behavioural) power stage" section. This 300-ceiling fix stays load-bearing for every OTHER processor on this solver (and for the Super Lead's
own reference/non-default netlist), and is the right first move before reaching for a behavioural replacement -- it is cheap, evidence-based, and has zero fidelity cost, whereas a behavioural
power stage is a real, user-approved exception to circuit fidelity and should only be reached for once a genuine worst-case block spike remains after this kind of solver-level fix.
