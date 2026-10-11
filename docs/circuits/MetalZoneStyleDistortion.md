# Metal Zone-Style Distortion

Display name **"Metal Zone-Style Distortion"**, registry key `MetalZoneStyleDistortion`, class `MetalZoneStyleDistortionProcessor`.
Runs on [`NodalCircuit`](./NodalCircuitSolver.md), **five blocks per channel** (a block holds at most one saturating op-amp), four
`addOpAmpMacro` stages that can clip, one linear op-amp, six real BJTs, one JFET and two diodes. (The pedal is the Boss MT-2 Metal Zone; the
"-Style" convention applies, see `PositiveGroundBooster.md`.)

## Source
The circuit diagram of the **service manual, MT-2, April 1991** (github apachiww/guitar_stompbox_collection
`Roland_Boss/Boss-MT-2-Metal-Zone-Schematic.pdf`), read at ~900 dpi. A manufacturer drawing: every value used is printed on it. All op-amps are
1/2 M5218AL, the JFETs 2SK184GR / 2SK118Y, the transistors 2SC3378GR, the diodes 1SS133. 9 V supply, a 4.5 V reference (the drawing's up-arrows).

## What the circuit is (signal order)
1. **Input**: R059 10K, C042 0.047 uF into Q011 (a JFET follower, gate 1M to the reference, R060 10K), C039 1 uF, **Q009 as the electronic-bypass
   series switch (modelled as 200 ohm, on)**, C033 0.015 uF, R043 100K into IC3b's (+).
2. **IC3b, first gain stage (block A)**: non-inverting, feedback R044 220K || C032 100 pF, and the (-) leg is **C034 0.027 uF into a bootstrapped
   emitter-follower branch** (Q010: C035 0.01 uF to its base, R046 2K2 to its emitter, R053 47K, R054 10K). Below ~340 Hz the base is an AC ground
   so R046 grounds the leg and the gain climbs to 1 + 220K/2K2; above it the base follows the signal, R046 sees no voltage, and the gain falls
   again: a **band-pass gain hump**, measured 34 dB at 800 Hz, -1 dB at 50 Hz and 7 dB at 12 kHz.
3. **IC3a, Dist (block B)**: R045 10K into (R042 10K || C031 0.047 uF) then C029 0.033 uF and R040 100K: a low-pass near 1 kHz. Non-inverting; (-) leg R041 1K +
   C030 10 uF to ground; feedback **C028 47 pF || (Dist pot 250KA as a rheostat + R051 1K)**. Gain 2 to 252.
4. **The clipper (block C)**: C027 10 uF, R033 2K2, **two 1SS133 back to back to ground** (symmetric ~±0.55 V), R032 10K, R031 4K7 + C023 0.015 uF, C021 1 uF, R029 100K into
   **IC4b** (non-inverting, feedback R030 3K3 || C022 47 pF, and **two bootstrapped-follower branches on its (-) leg**: Q008 with C024 0.015 uF / R034 1K / C025
   0.0015 uF and Q007 with C020 0.22 uF / R027 470 / C017 0.047 uF). Then **IC4a**, a unity-gain inverter (R028/R026 22K, C018 10 pF).
5. **Tone, first half (block D)**: R014 22K into node 7, IC1a non-inverting with R015 22K || C010 10 pF from its output to node 8. **High (VR03b 100K)** and **Low (VR03a 100K)** both go
   between node 7 and node 8 with their wipers to shunts: High's through R061 2K2 + C044 0.01 uF to ground; Low's into C008 0.22 uF and a bootstrapped high-pass (IC1b a
   follower, R012 2K2, C009 0.047 uF, R013 100K).
6. **Tone, second half and output (block E)**: C011 1 uF, then IC2a inverting with R038 47K / R035 47K || C026 100 pF, and its (+) input Z (R039 1M) fed by C038 0.1 uF from node 10.
   **Middle (VR02b 100K)** sits between R050 330 (from the input side) and R049 330 (from IC2a's output); its wiper goes into a follower (IC2b), C036 0.022 uF and R048 2K2. **Mid Freq
   (VR02a, 50K x2)** is two rheostats: node 10 to that R048 node, and node 10 to R062 2K2 to ground, with C043 0.0082 uF to ground on node 10. Then C005 10 uF, **Level (VR04 50KA)**, C004 1 uF,
   Q002 (the effect-side switch, 200 ohm), C002 10 uF, Q001 an emitter follower (R002 10K), C001 10 uF, R001 1K to the jack.

## Model
* Op-amps: `addOpAmpMacro`, 1e5 gain, 2.5 MHz, 75 ohm out, swing 1.5-7.5 V. IC4a a linear op-amp (its input is IC4b's rail-limited output, so it cannot clip in turn) sharing IC4b's block.
  IC1b and IC2b are ideal followers. A block cut between op-amps is a source plus the macro's 75 ohm (`NodalCircuit` restriction).
* Q010/Q008/Q007: real Ebers-Moll, `Is 1e-14, betaF 300` (the 2SC3378GR's gain is not on the drawing). Q011: `addJfet`, Idss 4.5 mA, Vp -1.2 V.
  Q009/Q002 are 200 ohm switches (the bypass electronics, the flip-flop and its diodes, are not modelled); Q001 an ideal follower with 0.65 V drop.
* The 1SS133s use the project's 1N4148 model.
* Pots: Dist 250K audio, clockwise = more; Level 50K audio; **High, Low and Middle 100K "G" taper assumed linear**; clockwise = boost (chosen by measurement: the wiper end that raises
  each band); **Mid Freq is a 50K "C" taper (`pots::law(x, 0.85)`), clockwise = LESS resistance = higher frequency**: measured on the network more resistance moves the boost lower, and
  the real knob's direction is an assumption.

## Verification (`Tests/MetalZoneStyleDistortionProcessorTests.cpp`)
* DC point: every op-amp output at 4.500 V, the jack at 0 V.
* IC3a's gain vs the closed form (finite GBW, input divider **including the C029/R040 branch that loads it**): within 0.15 dB at Dist 0.2 / 0.5 / 0.8, 400 Hz and 1.6 kHz (my first formula
  left the C029/R040 load out and failed by 0.36 dB; that was the test, not the model).
* IC3b's gain hump (above). Dist raises the gain, Level the output, monotonically.
* Small-signal ranges: **High +23 dB at 6.4 kHz (1 vs 0), Low +38 dB at 100 Hz, Middle +23 dB at 400 Hz (1 vs 0)**; with Middle at 1, the boost sits around 400-800 Hz at Mid Freq 0.75 and 1.6-3.2 kHz at
  Mid Freq 1.0. These ranges are what the netlist gives with the pots' ends wired as drawn; I had no reference measurement of a real MT-2 to compare them against, and the pot
  taper ("G") is an assumption, so treat the exact dB figures as unverified.
* Hot input (3 V) and random knob moves: finite, solver failure rate 0, 5.6 Newton iterations/sample over the five blocks. Steady state clean (-178 dB).

## Oversampling and cost (the honest part)
**~15% of a core at 1x, before any oversampling** (~30% at 2x): far above the 5% target, and each block costs at least one Newton iteration (~2000 cycles), so five blocks is a floor for this
netlist. Alias (non-harmonic/harmonic, `PEDAL_ALIAS=1`, a hard-driven probe): 1x -21.9 dB, 2x -27.8, 4x -39.9, 8x -75.8: **the worst of the pedals so far, because IC3b, IC3a, the diodes and IC4b all clip in
turn**. Orders {0, 1, 2} like the HM-2. Unity trim +2.23 dB. A cheaper model (fewer blocks, a reduced clipper) is possible but would have to be checked by ear against this one first; none is built.

## Reduced-order front-end attempt (2026-10-11 -- abandoned, kept for the next try)
A companion-model substitute for blocks A-C (input buffer through the diode clipper) IS implemented in
`MetalZoneStyleDistortionProcessor.cpp` behind `reducedOrder` (static, off by default -- the registry comment
says why). Every capacitor keeps its trapezoidal companion model; the four op-amps become closed-form
saturating-integrator solves (`ReducedAmp`, exact inside the rails); the JFET follower is a per-sample Newton
(it sits near pinch-off where a fixed gain lies); the diode clipper is a scalar Newton; and the two
bootstrap-follower legs fold into the m-node KCL as **affine** admittances (ideal 0.65 V-drop followers).
Per-component, all of it is exact except those legs -- which is exactly where the fit dies:

| configuration (measured vs own five-block netlist, ReducedOrderEquivalence harness) | worst level | worst band |
|---|---|---|
| affine legs + JFET Newton (what is committed, off) | ~5-24 dB | ~8-14 dB |

Bar is level <2.0 / band <1.2. The error concentrates at Dist>=0.5 and mid-boost: the legs' 2SC3378s need a
real BJT knee (Vbe curve, can't-sink cut-off, iB = iE/(beta+1)). A real Ebers-Moll leg WAS written (3-node
inner fixed-point per leg) and abandoned: putting a nonlinear device on the m-node destroys the stage's
closed-form solve, and every iteration scheme tried failed differently -- full Newton on v_m limit-cycled
across the rail knee, secant/quasi-Newton on the legs did not converge in 6 iters (amp latched its parasitic
rail state), and evaluating the legs one sample behind kept stability but returned ~-24 dB level errors.
The honest conclusion: this stage wants the legs and the m-node in ONE simultaneous Newton (a small real
solver, not a scalar trick) -- at which point it stops being cheaper than the block it replaces.

Kept for reuse: `ReducedFrontEnd`/`ReducedAmp`/`BootstrapLeg`, the MZ_EXPLORE per-stage diff probe in the
tests (measures reduced-vs-netlist error at ic3b/ic3a/ic4b/ic4a), and the equivalence harness pattern.
The pedal stays at eco 2x.

## Reduced-order front-end, second attempt (2026-10-11 -- works, still misses the bar)
The same companion model, but the legs now carry the REAL one-port 2SC3378 (vbe exponential,
base current, no collector side -- the collector sits on a stiff 9 V rail so the reverse junction
never conducts and the one-port is exact while vbc stays deeply reverse). `BootstrapLeg::eval`
solves a warm-started, junction-limited 2x2 Newton on (vb, ve) with v1 eliminated analytically,
and `stageNewton` solves the m-node KCL on vm.

Two fixes that mattered:
- **Bracketed stage Newton.** F(vm) = kFb*vo(vm) - gSum*vm + hm - iLegs(vm) is continuous and
  monotone decreasing, but the integrator makes vo swing ~196 V/V inside a ~30 mV linear window,
  so an unbracketed Newton slam-cycles between the railed equilibria (the failure the first
  attempt documented). Keeping a sign bracket on vm (Newton inside, bisection when the step
  would leave it) converges unconditionally and quadratically in the window.
- **solvePx iterate bound.** Without it, a wild vAmp throws the integrator node volts past the
  rails, the clamp-diode exp()s dwarf every other term, and px latches at ~1e5 with the output
  pinned high forever -- that was the -nan render.

Measured (ReducedOrderEquivalence, same harness): worst level 1.02 dB (was ~5-24), worst band
**5.68 dB** (was ~8-14) at dist 0.9 / mid 0.8 / in 0.3 -- the 1.6-12 kHz octaves run 3-6 dB hot at
dist>=0.5, in>=0.1. Per-stage probe: ic3b error down to 3.95 mV rms (was >3 V), ic3a ~20-45 mV,
ic4b up to 213 mV at dist 0.9. Still over the 1.2 dB band bar, so the flag stays off; measured
eco cost if it shipped: **48.4% -> 27.7%** of a core. Solver failure rate 0.000000.
The remaining HF excess suggests the last miss is in the IC3a/IC4b clip region, not the legs.

## Reduced-order front-end, third pass (2026-10-11 -- fit CLOSED, blocked on CPU now)
The remaining 5.68 dB band gap was NOT in the clip region after all -- it was three modelling
bugs the band-delta had been smearing across the 1.6-12 kHz octaves:

- **Wrong stage-output node.** The macro treated each op-amp stage's output as the stiff `o`
  node, but the netlist's real `o` droops under its in-block loads. The companion model now
  solves vo = (ob + rOut*(kFb*vm - fbIeq + vOutExtraSum)) / oDenom per Newton iteration, with
  vOutExtraSum/gOutExtra folding in the extra conductances each stage's o-node actually sees.
- **Clip Newton without junction limiting.** A hard step overshoots past the diode knee and
  the iterate sheds ~nVt per iteration -- vk never reached the clamp inside the budget and
  the node ran effectively unclamped. pnjlim (same helper NodalCircuit/EbersMollBJT use)
  fixes it; clip solve now converges in ~2 iters.
- **Block-boundary impedance** (the macro's o-node is the real 75 ohm output, not the 150R
  the first macro assumed).

Measured (ReducedOrderEquivalence, same harness): **worst level 0.01 dB, worst band 0.04 dB**
across the whole dist/mid/input grid -- vs bar 2.0/1.2. DI render of the enabled reduced path
is spectrally identical to the full path (<0.1 dB every octave band, same level/energy). The
fit is done.

**Solved 2026-10-11 (reducedOrder enabled):** the faithful nested solves cost ~3x the netlist
they replaced (~67% vs ~21% eco render, ~127% vs ~51% bench) because the legs' inner 2x2
(vb, ve) solve dominates -- ~40 stageNewton + ~100 legNewton iterations per sample. Both
nested solves are now replaced by fitted curves:

- **BootstrapLeg LUT.** The leg's interior solve parameterises to exactly two scalars:
  u = c1.g*vm - c1.ieq + c2.ieq (drive + cap histories) and q = c2.ieq (base-KCL history).
  `buildLegLut` sweeps (u, q) over the runtime-measured domain (`legLutDomains`, scaled by
  fs/48k) once per sample rate under a mutex, warm-starting each cell from its neighbour
  and the DC point -- and tabulates {v1, vb, ve} on a 1024 u-grid x 5 q-slices.
  `eval()` serves it with a Catmull-Rom cubic in u and a quartic (Lagrange) through all
  5 q-slices; dIdv is recomputed by the closed form at the interpolated (vb, ve).
  Out-of-domain (u, q) falls back to the real solveU.
- **Legless stage Newton -> closed form.** With no bootstrap legs the stage F is affine in
  vm per operating region, so `affineRoot` solves it directly and commits when the root
  lands inside its region (linear band, or >0.5 V past a rail); only the rail-transition
  band still runs the bracketed Newton (tol stays 1e-10 -- 1e-6 left the iterate
  under-resolved and showed up as ~-69 dB of non-periodic chatter on the steady-state
  test, vs -144 dB at 1e-10; the LUT evals are cheap enough that the extra Newton
  iterations cost almost nothing).

Result: `RenderFx --bench` reduced ~30-36% vs high ~57-75% (eco render ~26%), and
ReducedOrderEquivalence measures 0.46 dB worst level / 0.51 dB worst band vs the full
netlist -- inside the #71 bounds (2.0/1.2). The LUT-induced delta vs the exact Newton is
the whole of that residual (the LUT-less build still measures 0.01/0.04).

**Watch out:** the leg is multi-valued at u < ~0 (the degenerate region where the bootstrap
emitter can't source) -- uLo bounds were chosen to exclude it; extending the domain into it
tabulates a different solution branch than runtime Newton finds and the error is silent
(the static verify sweep can't see it). A denser 7-slice q layout regressed to 17.7 dB
worst-band in the mid-0.2 configs for a not-fully-root-caused interpolation-side error
(all hits, clean static sweep, no drift); the 5-slice layout above is the verified one.

Other over-budget nodal pedals (BassBigMuff ~27%, TubeDriver ~24%, ODR1 ~23%) don't share
this problem shape -- their cost is spread over the whole netlist solve, not one nested
per-leg loop parameterised by 2 scalars, so the trick does not transfer.
