# Bassman-Style Amplifier (Fender Bassman 5F6-A, 4x10 "tweed")

Display name **"Bassman-Style Amplifier"** (category Amplifiers). Runs on
[`NodalCircuit`](./NodalCircuitSolver.md) with its tube, coupled-inductor and current-source
elements. Code: `Source/Effects/BassmanStyleAmplifierProcessor.{h,cpp}`,
`Source/Effects/TubeModels.h`. Tests: `Tests/BassmanStyleAmplifierProcessorTests.cpp`,
`Tests/TubeCircuitTests.cpp`.

## Sources
1. The factory schematic and layout (two scans, model 5F6-A, "voltages read to ground with an
   electronic voltmeter, values shown +-20%"): 12AY7 V1, 12AX7 V2 (gain stage + cathode
   follower) and V3 (phase inverter), two 5881, GZ34, power transformer 8087, choke 14684,
   output transformer 45249.
2. Kuehnel, *The Fender Bassman 5F6A Circuit* (pentodepress.com/summary.html): stage-by-stage
   small-signal analysis (gains, impedances, tone-stack limits), the power amplifier's measured
   open-loop transfer curve, and the power supply's sag response. It is the reference every
   test compares against.

The revision (5F6-A) was confirmed with the user before starting.

## What is modelled

```
guitar -> [pre block]  V1A/V1B 12AY7 (shared cathode 820 R || 250 uF) -> 0.02 uF -> 1M audio volume pots
                       (Bright: 100 pF across the pot's top) -> 2 x 270k mixer -> V2A 12AX7 (820 R unbypassed)
                       -> V2B cathode follower (an ideal follower with the DC drop of the real one)
       [power block]   -> 531 R (follower output impedance) -> TMB tone stack (250 pF, 56k, 250k treble,
                       1M bass, 25k middle, 2 x 0.02 uF) -> 0.02 uF -> V3 12AX7 long-tailed pair
                       (82k / 100k plate loads, 47 pF between plates, 470 + 10k tail, 1M grid leaks)
                       -> 2 x 0.1 uF -> 2 x 220k grid leaks to the -48 V bias -> 2 x 5881/6L6 beam tetrodes
                       (screens through 470 R) -> output transformer 45249 (4k plate-to-plate, 2 ohm secondary)
                       -> 2 ohm speaker
                       <- 27k from the speaker terminal into the presence network (5k, 0.1 uF) -> 0.1 uF -> V3's second grid
       [supply model]  GZ34 + transformer resistance -> 40 uF (plates) -> choke -> 20 uF (screens)
                       -> 4.7k -> 20 uF (phase inverter) -> 10k -> 8 uF (preamp)
```

Controls (0..1), **page 1**: **Input** (added 2026-09-22: Normal / Jumped / Bright), Volume (Normal), Volume (Bright),
Treble, Middle, Bass, Presence, Output. The real amp has no master volume; **Output is a plug-in level control**
(-30..+12 dB after the speaker terminals). **Page 2** (added 2026-09-21, see "Page 2" below): Power Drive, Bias,
Tube Feel, Speaker. Input 2 of each channel and the 1M input resistors are not modelled (the guitar is an ideal
source with a DC path).

### The three solver blocks and why the cuts are where they are
### Input (2026-09-22): Normal / Jumped / Bright
On the real amp, each channel's own jack drives its own triode's grid stopper -- V1A (Normal) and V1B (Bright) each have
their OWN 68k resistor back to their OWN input node, not a shared one. The model used to tie both to a single node,
i.e. it was **always** wired as if a jumper cable connected the two channels (the classic Bassman/Twin trick of patching
Bright-1 into Normal-2 to blend both channels from one guitar) -- so Volume (Bright) always did something even with
nothing conceptually "plugged into" that channel. The three-way selector (`bm_input`, a vertical selector like Speaker's)
now actually gates which channel's node is driven: **Normal** feeds only V1A (V1B's grid sits at 0 V signal, only its own
1M bias resistor holding its DC point -- Volume (Bright) becomes inaudible, matching a real amp with nothing in that
jack), **Bright** the mirror, **Jumped** (the default, matching the model's old and only behaviour) drives both.
Verified in `Tests/BassmanStyleAmplifierProcessorTests.cpp`: the disconnected channel's own Volume knob changes the level
by under 2% (a real, small effect -- its triode's plate resistor and pot still LOAD the shared mixing node even with no
signal on its grid, exactly as an unused real tube sitting there would), never anywhere near the connected channel's own
level. Jumped measurably exceeds either single channel.

**Also found while adding this**: playing through a single channel (Normal or Bright alone) drives the mixing node and
V2A half as hard as Jumped does for the same Volume settings, which is a large part of why "everything sounds broken with
both volumes maxed" (below) -- Jumped was quietly doubling the preamp drive the whole time.

* **Preamp** (6 Newton ports): V1A, V1B, V2A. Cut after V2A's plate: the cathode follower's grid
  draws no current, so nothing downstream can load it back. Its output impedance (531 ohm, Kuehnel)
  and gain (0.984) are applied on the other side of the cut.
* **Power section** (8 ports): tone stack, phase inverter, power tubes, output transformer and the
  global feedback. **One block on purpose**: the feedback loop closes through it. A one-sample delay in
  a loop with ~6 dB of feedback would change its frequency response and phase margin; solving the
  loop exactly costs about the same as two blocks and removes the question.
* **Supply** (a separate 6-node linear NodalCircuit run every 8th sample): the plates' and screens' current
  draw (from the power block's tube currents, averaged over the interval) drives the rails
  `+452 / +450 / +385 / +325 V`, which are fed back as source voltages. The loop is slow (ms), so the
  delay is harmless. No ripple: Kuehnel's 120 Hz attenuation figures are 42 dB at the plates, 83 dB at the
  screens, 120 dB at the phase inverter and 156 dB at the preamp, so only the plates' would be audible, and
  hum is not part of the tone being modelled.

### Tube models (`TubeModels.h`)
* **Triodes**: Koren's plate current with Dempwolf's grid current. `ip = 2 E1^Ex / Kg1`, `E1 = vpk/Kp ln(1 +
  exp(Kp(1/mu + vgk/sqrt(Kvb + vpk^2))))`, `ig = Gg (ln(1 + e^(Cg vgk))/Cg)^xi`. 12AX7: Koren's ECC83 set
  (mu 100, Ex 1.4, Kg1 1060, Kp 600, Kvb 300); grid current Gg 6.177e-4, Cg 9.901, xi 1.314.
  **12AY7**: not in the literature this project has; mu 54.3, Kg1 986, Kp 172.6 (Kvb 300, Ex 1.4) were fitted
  to Kuehnel's operating point (150 V, 1.5 mA, mu 49, rp 29.9k): it reproduces 1.36 mA, gm 1.46 mS, rp 27.2k.
* **Beam tetrodes (5881/6L6)**: Koren's pentode with the screen voltage supplied per sample (not a circuit
  node: the screen resistor's drop is applied one sample late with a 3-sample smoothing, and the rail
  sag comes from the supply model) and a plate-resistance term `(1 + lambda (vpk - 400))`, lambda 6e-4.
  **mu 15.96, Kg1 889.4, Kp 23.2 were fitted** (Ex 1.35, Kvb 12, Kg2 4500 fixed) so that the push-pull
  stage's static transfer curve through the 4k transformer reproduces Kuehnel's composite curve (grid
  +-48 V -> +-13 V into 2 ohm: slope 0.18 at the centre rising to 0.27 at the extremes). Koren's standard
  6L6GC set (mu 8.7, Kg1 1460, Kp 48) is ~1.7x too linear around the Bassman's -48 V bias (its
  open-loop third harmonic was 0.8% against the published 5%). A plate above 1.1 kV flashes over
  (`arcVoltage`, 25 ohm, 20 V knee): irrelevant in normal use, but without it a hard-overdriven stage
  lets the transformer's inductive kick run to kilovolts and the solver loses the operating point.
* Run-time evaluation is by **cubic Hermite tables** (`HermiteTable`; `(vpk/Kp)^Ex`, `softplus(a)^Ex`, the
  grid current, `atan(vpk/Kvb)`, all shared per parameter set and built on the control thread): a call is a
  sqrt and 2-4 lookups. Agreement with the formulas: 2e-6 relative (`TubeCircuitTests`).
* Interelectrode capacitance: only grid-plate (1.7 pF: the Miller effect that shapes the top end against the
  268k mixer, the 68k stoppers and the tone stack's 1.27M output impedance). Grid-cathode and plate-cathode
  (0.5-6 pF) are not modelled: against these source impedances their corners are above 1 MHz (68k x 1.6 pF
  = 1.5 MHz, 100k x 0.46 pF = 3.5 MHz) and each capacitor costs solver state on every sample.

### Page 2: Power Drive, Bias, Tube Feel, Speaker
* **Power Drive** (default 1 = the original amp): a master volume between the preamp and the tone stack / phase
  inverter. The cathode follower's excursion is scaled (amplitude = knob^2, an audio taper); its DC level and the tone
  stack's impedances are untouched, so at full it is exactly the calibrated amp. It sets how hard the phase inverter and
  the output tubes are driven independently of the preamp, which the real amp cannot do. **History (2026-09-21):** the
  first version divided the signal at the power tubes' grids (a series resistor, then a potentiometer in place of the
  220k leak). Every failing case of a random-knob stress had Power Drive below a third: with the master AFTER the phase
  inverter, a hot preamp overdrives the inverter with nothing at the output tubes' grids to clamp it (no grid current),
  and the coupled high-gain stages lose their operating point. A master in front of the inverter keeps it out of that
  regime; it is also where most amps put theirs.
* **Bias** (default 0.5 = -48 V): the grid supply, -38 V (hot) .. -58 V (cold). A source in the netlist changed
  per control tick (no matrix work); its 8 uF / 11.8k time constant makes it move over ~100 ms as on the real
  amp. Measured idle current (both tubes): cold 42 mA, noon 79 mA, hot 140 mA; the supply sags harder when hot.
* **Tube Feel** (default 1 = the real amp): scales how much the amp behaves like a tube amp: the supply's
  series resistance (sag: deepest screen droop 12 / 42 / 65 V at 0 / 0.5 / 1) and the feedback (0 = 2.5x the
  negative feedback, 1 = the original; more feedback = less gain, stiffer, closer to solid state). The supply's
  open-circuit voltage follows so the idle rails stay on the schematic's values. For the Bassman it is not a
  blend of two signals: it moves physical parameters. Common to the amps modelled here (not to the neural ones).
* **Speaker** (default 8 ohm): 4 / 8 / 16 ohm (a selector: the editor shows the words). The load is a speaker,
  not a resistor: voice coil (Re 6.5 ohm, Le 0.55 mH for 8 ohm; everything scales with the nominal impedance) in
  series with the cone's mechanical resonance (85 Hz, Q 5, ~38 ohm peak, as a parallel RLC). The output
  transformer is an **8 ohm-tap design**, so 4 and 16 ohm are mismatches (2k / 8k plate-to-plate instead of 4k):
  4 ohm loads the tubes harder (measured full-drive output 5.3 V rms), 16 ohm lightly (13.7 V, against 9.8 V at
  8 ohm). Because the presence network takes its feedback from the speaker terminals, the impedance curve enters
  the loop: the amp's response bumps +4.5 dB at the cone resonance (the "thump") and lifts +6.6 dB at 6 kHz against a
  resistor, and the top end peaks near 10 kHz where the voice coil's inductance resonates with the transformer's
  capacitance. The output is the terminal voltage (an impulse response measured with a voltage source drives the
  cabinet block correctly); it is peakier than a resistive load's, up to ~2.5x the RMS at hard drive.
* Changing the speaker at run time changes capacitor and inductor values (`setCapacitance`,
  `setInductorInverse`, precomputed inverse matrices, no allocation); it clicks slightly if changed mid-note.

### Output transformer
Three coupled windings (`NodalCircuit::addCoupledInductors`, trapezoidal companion model with the full
inverse-inductance matrix): two primary halves (6.25 H each, i.e. 25 H plate-to-plate, wound against each
other from the centre tap so the DC plate currents cancel in the core) and the secondary. **Since 2026-09-21 the
secondary is an 8 ohm tap** (turns ratio (4000/8)^0.5 / 2 per half; the original 45249 is a 2 ohm design) so that
the Speaker control can present 4 / 8 / 16 ohm; the 27k feedback resistor became 54k (the same fraction of the
output) and the digital output scale 1/40 (twice the volts for the same power). The primary carries a 47k loss resistance and 300 pF per plate to ground (see the instability note below). Tests that compare against
Kuehnel's measurements (made into a resistor at the 2 ohm tap) use `debugSetResistiveLoad(8)`, which reproduces
them unchanged. 250 pF plate-to-plate models the primary's distributed capacitance. Coupling 0.9997 between halves and 0.9992 to the secondary (~25 mH
of leakage, a top-end corner near 12 kHz), 45 ohm per primary half and 0.06 ohm secondary resistance. Load: a
resistive 2 ohm. **Assumptions**: the real 45249's inductances, leakage, and winding resistances are not
published in the sources used; these are typical values for a transformer of that size. Core saturation
and interwinding capacitance are not modelled.

### Global feedback
27k from the speaker terminal into the presence network, coupled by 0.1 uF into V3's second grid. The
presence pot (5k linear, 0.1 uF on the wiper) sets how much of the top end is fed back: at minimum the
feedback node sees 5k to ground (feedback input impedance 32k against Kuehnel's 31k), at maximum the cap
shorts the top end (27k). With Presence at 0.3 the loop is ~6 dB. The gain to the plates from the
feedback input is 0.156 x the phase inverter's gain, which is Kuehnel's +3.4 / -3.5.

**Power section integration (theta 0.9, found 2026-09-21).** A 10 s soak at 16 ohm with Presence and Volume up still
lost about a hundred samples (Newton failure, held output: an audible tick) after the damping above. The pedals
integrate capacitors and inductors with theta = 0.6 (a trapezoid rule with a little damping); on this stiff, high-gain
circuit the trapezoid rings at Nyquist whenever a tube cuts off. Theta 0.9 in the power section (the preamp and supply
stay at 0.6 and 0.5) removes those failures at all three loads (0.6: 100 failed samples per 10 s; 0.75, 0.9 and 1.0:
none) and costs a slight damping of the very top of the band; every published-figure test still passes. The random-knob
stress (40 combinations of every control, 2 ms pick attacks, 8 and 16 ohm) is now a permanent test, and the supply is
protected from a solver excursion (the current it sees is clamped to what two 5881s can draw, the rails to 0..open
circuit).

**Instability at 16 ohm and the "stuck" solver (found by the user 2026-09-21, fixed the same day)**: after a few
minutes of playing the amp's CPU went to ~1000% and the sound froze; at Speaker 16 ohm it also crackled first. Cause:
the closed loop with the speaker's voice-coil inductance reflected through the transformer resonates with the
transformer's capacitance; with a 16 ohm load (8k plate-to-plate, a mismatch on the 8 ohm tap) the loop went
unstable on a plucked note and the plates grew ~20% per sample to kilovolts. The solver then never recovered: a
failed sample leaves the state frozen and the next one retries, so it stayed stuck at 100 Newton iterations x 3
fallbacks per sample. Fixes: (1) **47k across the primary** (core and copper loss) and **300 pF from each plate to
ground** damp that resonance (measured: 10 s of plucked notes, Presence 10, volumes 8: failures per second went
from 6380-48000 to 0 at 16 ohm; either element alone was not enough); (2) **recovery**: 48 consecutive failed
samples restore the settled state saved by `prepare()` and re-apply the knobs (`recover()`), input NaN/inf is
read as 0 and a non-finite output as 0; (3) `Tests` now include a 10 s soak per speaker load and a NaN/1e6
garbage burst, which the short sine tests could not have found. (4) **Level compensation** across the loads
(`speakerGain = (Znom/8)^-0.8`): the raw volts follow z^0.83, so 16 ohm was 3 dB louder and much peakier.

**Stability at 48 kHz (found 2026-09-21)**: with an inductive speaker the closed loop was unstable at input
levels a guitar can reach (Newton lost the operating point at the corner bass 10 / mid 0 / treble 10, both volumes
10). It was stable at 96 kHz, so the cause is discretisation: the loop's gain does not fall before this model's
Nyquist, because the transformer and tube capacitances that make the analogue amp stable there cannot be
represented above 24 kHz. A 1.5 nF capacitor from the feedback node to ground (a ~25 kHz pole with the 4-5k the
presence network leaves there) fixes it and costs 0.7 dB at 10 kHz. It is a modelling device, not a part of the amp.

**Schematic-reading note (a real ambiguity)**: the feedback's exact entry point is hard to read on the
scan; the topology above is the one consistent with Kuehnel's numbers (a 27k : 5k divider, 0.156, times the
second triode's gain 22 = 3.4; input impedance 27-31k) and with the schematic's node values. The phase
inverter's DC labels (+22/+23 V, +32.5 V, +54 V) do not all fit one reading; the tail at 32.5 V (10k x
3.25 mA) and the grid leaks returning to it (bias -1.6 V) is the only one that gives a sensible bias for
plates at 230/236 V.

## Verification (`Tests/BassmanStyleAmplifierProcessorTests.cpp`)
| Quantity | Published | This model |
|---|---|---|
| V2A plate / +452 / +450 / +385 / +325 rails / bias | 180 / 452 / 450 / 385 / 325 / -48 V (+-20%) | 189 / 452 / 450 / 385 / 325 / -48 V |
| Phase inverter plates / tail | 236 & 230 / 32.5 V | 255 & 246 / 29.7 V |
| V1 gain (12AY7, bypassed) | -32.2 | -30.1 |
| V2A gain (one channel at full, other muted) | -20.7 | -21.4 |
| Phase inverter gain (open loop), balance | -21.9 / +22.6 (1.032) | 27.8 / 28.7 (1.033): the Koren 12AX7 has a higher gm than Kuehnel's mu 101 / rp 57.7k |
| Power stage open loop: output at 48 V grid | ~13 V | 11.7 V |
| ... slope centre -> extremes | 0.175 -> 0.275 | 0.19 -> 0.244 |
| ... third harmonic at full swing, no sag | 5% | 5.5% |
| Screen sag at full power | -57 V, first minimum ~-62 V at ~50 ms, 13 Hz ring | -53.7 V, -64 V at 48 ms |
| Output at full drive (2 ohm) | ~45-50 W | 52-54 W |
| Presence 0 -> 1 | (+ top end) | +5.1 dB at 5 kHz, -0.1 dB at 80 Hz |
| Bright cap at volume 0.3 | (treble boost) | +9 dB tilt over the normal channel |

Also: DC and 400 Hz operation at all 32 corners of the tone/presence/volume knobs; no Newton failure at
input levels 10 mV to 1.0 (0 dBFS) with both volumes at 10; dual-mono shortcut identical to the mono
run.

## A hot pedal into the amp (2026-09-21): what fails, and what was done about it
User report: a pedal with its Level far up, then the amp "starts to pop". Reproduced (`Tests/PedalDeathProbe.cpp`, env
`BASSMAN_PROBE=<pedal key>`, `BASSMAN_PROBE_SCALE`): a Guv'nor with every knob at its maximum (0.95 V peaks into the amp) into
the Bassman at defaults made the **power block** lose the operating point 2x in 30 s (each one a `recover()`: the amp is reset and
the output jumps), 62x at 2 V and 157x at 3 V, and printed sample jumps of 13 V (23x the running rms), at worst 26 kV.
* **What fails**: no failure below ~0.6 V of peak input; above it, sporadic. The states just before a failure are already
  nonsense (a power plate at -177 V, a grid at +38 V, `toneOut` at -5387 V within one block): the output transformer flying
  back on a hard edge, and a "converged" solve on a branch that no amplifier reaches (the plate arc term, 1100 V, creates
  one). `theta = 1` (backward Euler) fixes the 0.95 V case but not 2 V; a forcing-continuation fallback, a clamp on the cathode
  follower's excursion (60/100/150 V) and rejecting small-step convergence with a large residual were each tried and did
  **not** remove it. The root cause (the power stage's stability under sustained saturation) is **still open**.
* **What was changed** (mitigation, not a cure):
  1. *Input limiter*: a soft limit above 0.4 V of peak (tanh, ceiling 0.65 V) ahead of V1. A guitar peaks at 0.3-0.5 V and V1
     (gain ~30) is not near clipping at 0.65 V, so nothing below the knee changes; the top of a very hot pedal is taken off.
     Recoveries per 30 s: 2 / 62 / 157 -> 0 / 0 / 5 (Guv'nor at 1x / 2x / 3x); with an HM-2 (3.2 V peaks) 5-11 remain, a RAT 0-1.
     **It is a deviation from the real amp** (which has no such limit), taken because the alternative is a solver reset.
  2. *Sanity check*: a sample whose speaker terminal is beyond +-150 V (the real amp reaches ~50 V) counts as a failed solve and
     the output holds its previous value instead of printing it.
  3. *Continuity across `recover()`*: the last emitted sample is remembered and the offset to the restored state's output
     decays over ~5 ms, so a reset is a short thump, not a step (the step was the pop).
  4. *Recovery after 8 consecutive failures, not 48* -- the CPU half of the same problem. A failing sample holds the output
     (so a 48-sample streak was 1 ms of frozen sound anyway) and costs ~100-300 Newton iterations; the streak made one
     128-sample block cost **13.0 ms against a 2.67 ms budget** (a dropout; the user saw the CPU meter pass 100% and heard
     the sound "travando"). With the solver's own fallback cap (`NodalCircuitSolver.md`) and this, the same block costs
     **2.19 ms** and nothing goes over budget: measured over 20 s of a distortion pedal at full gain into the amp into a cab,
     max 497% -> 82% of budget, 2 blocks over budget -> 0, and the amp's failure rate fell 4x.
  Regression tests: "a hot pedal into the amp" (10 s of square-ish notes up to 3 V: finite, bounded, no long streaks, and
  **average Newton iterations per sample in the power block under 12** -- a deterministic stand-in for the wall-clock cost);
  the random-knob test now allows a one-sample blip (rate <= 1e-4) but still no recovery. `Tests/ChainSpikeBench.cpp`
  (`CHAIN_SPIKE=1`) is the profiler that found it: it times every block of a pedal -> amp -> cab chain and prints the
  percentiles. Cost does **not** rise with playing level (amp alone 15.3% of a core at 0.05 V in, 15.8% at 0.9 V); what rose
  was the spike rate, which is what the user was seeing.
* Ideas not tried: replacing the plate arc term by a proper flyback clamp (a snubber across the primary), a smaller integration step
  inside the power block only, rejecting/rolling back the sample (the solver has no state rollback).

## Still bugging at extreme drive (2026-09-22): the same open issue, worse
User report: HM-2 at every knob maxed into the Bassman with Volume (Normal), Volume (Bright) AND Power Drive all also
maxed -- "ainda bugando o som" and the CPU calculation grows. Reproduced (`BASSMAN_KNOBS_MAX=1` on the same probe): **210
recoveries in 30 s** (a constant flurry of small resets, not a rare spike) and individual blocks up to 13 ms again --
the same root cause as above (the power stage losing its operating point under sustained hard saturation), reached this
time from the PREAMP's own controls (Jumped input + both volumes + Power Drive at max) rather than an external pedal.
* **A hard hidden clamp does not work here either.** Tried clamping the master-gain-scaled cathode-follower excursion
  that drives the power block (`BM_EXC_CLAMP`, swept 3-60 V): at every value loose enough to leave *legitimate* hard-drive
  excursions untouched (a normal hot signal at Power Drive = 1, the amp's own DEFAULT, reaches raw excursions over 130 V
  without ever failing -- confirmed against the existing passing test suite), it still left several recoveries; at values
  tight enough to reach zero it would also clip signals well inside the range the amp already handles correctly today,
  a real fidelity regression for no full fix. **Not shipped.**
* **Two things that DID help, and are real** (both mitigations, not the cure -- the root cause is the same "still open"
  line as above):
  1. **The Input selector** (see above): using a single channel instead of Jumped roughly **halves the preamp drive**
     for the same knob settings. Recoveries in 30 s: 210 -> 15 (Normal only), worst block 13.0 ms -> 4.46 ms. This is
     also simply how a guitarist normally plays (one cable, one jack) -- Jumped is the unusual, deliberately-both-at-once
     case, on the real amp too.
  2. **The recovery threshold lowered again, 8 -> 4 consecutive failures.** A failing sample already holds the output, so
     a shorter streak before resetting is both a shorter glitch and less wasted Newton work. Worst block with Input =
     Normal: 4.46 -> 2.91 ms (further, to 109% of budget -- 1 block over out of 7500 in a 20 s run, down from 4).
     Full regression suite unaffected (checked: no new recoveries in any of the existing knob-sweep/hot-pedal tests).
* **Not solved (first pass)**: with Input = Jumped and every preamp control still at its maximum, the amp still
  recovers dozens of times in 30 s and can still cost several ms on a single block. This is deliberately the most
  extreme corner the model can be pushed into (three controls simultaneously maxed, doubling the drive via Jumped) and
  was tracked as the same open power-stage-stability item.

## Root cause found, partially fixed, general (not a per-pedal patch) (2026-09-22)
User: "vamos resolver esse problema do estagio de saturação, tem q ser uma solução aplicavel pra tudo e q n sobrecarregue
a cpu e que mantenha a dinamica da distorção" -- a general fix, no CPU overhead in normal use, no clamping of legitimate
signal. Investigated with a real failure trace (not guesswork): captured the power block's exact Newton state at the
moment it gives up (`nv=8`, one 20 us sample). Found two real, general bugs/limits, both fixed in `NodalCircuit.h` and
`TubeModels.h` (full writeup: `NodalCircuitSolver.md`, "Tube model gradient dead zone, and an evidence-gated iteration
budget"):
1. The Koren triode/pentode and grid-current models had an ASYMMETRIC dead zone: the high edge of their tabulated range
   already fell back to an exact smooth formula, the low edge (deep grid cutoff -- exactly where an extreme grid
   excursion lands) returned a hard zero gradient instead. A zero-gradient device gives Newton nothing to steer by.
   Fixed to use the same smooth fallback both directions. This is a real correctness fix independent of the Bassman,
   and improves every tube circuit that can drive a grid hard.
2. Newton's last-resort fallback (mode 2, quarter steps) had a FIXED 100-iteration budget; a trace showed it creeping
   toward the correct answer at a small fixed rate and simply running out of iterations when the forcing had moved by
   hundreds of volts in one sample -- not diverging, just too slow for the clock. Now extended, evidence-gated: it keeps
   going (up to 900 iterations) only for as long as the step is measurably still shrinking, and gives up exactly as fast
   as before the moment it stalls. A normal sample never reaches this code at all (it converges in single digits of
   iterations), so ordinary playing and every other circuit pay nothing for it -- the fix's cost is proportional to how
   extreme the moment actually is, the property the user asked for.

**What this achieved, honestly**: the realistic case (a single guitar cable into ONE of the amp's inputs -- Bassman
Input = Normal, per the selector above -- with the preamp's controls maxed) improved from ~15-20 recoveries per 30 s
to **~6-9**, a real if partial fix. The most extreme corner (Input = Jumped -- both channels driven, doubling the
preamp signal -- WITH every control also maxed) is still not fully fixed: the extended budget revealed a NEW, more
precisely characterized failure mode -- Newton makes real, sustained progress for several hundred iterations and then
the proposed (pre-limit) step spikes by orders of magnitude at one specific iteration, while the actual clamped port
voltages stay physically reasonable throughout. This reads as a transient ill-conditioned Jacobian (a near-singular
linearisation at one exact operating point, possibly where two device derivatives momentarily cancel, or a different
tabulated function's own edge -- the atan/knee table, the screen-power table, or the arc-voltage onset -- is crossed)
rather than a state actually running away. Not chased further under time pressure; the next step is the same kind of
trace-driven investigation that found the two fixes above, not another threshold guess.
**Not touched (verified safe, not the cause)**: a hard clamp on the excursion driving the power block was tried again
and rejected again for the same reason as before -- it would clip signal the amp already handles correctly today.
Ordinary playing (no extreme knob combination) was re-measured after every change: 0 blocks over the 2.67 ms budget in
a 20 s stress run, mean cost unchanged.

## The near-singular-Jacobian corner, closed (2026-09-22)
Continuing directly from the section above, per the user's explicit ask to finish this ("faz a unica coisa q faltou
resolver... solução aplicavel pra tudo e q n sobrecarregue a cpu e que mantenha a dinamica"). Full technical writeup
(the two reverted solver-level attempts, and why): `NodalCircuitSolver.md`'s "The near-singular-Jacobian corner,
closed at its actual source, not patched around". Short version: a trace found the actual cause was a SECOND instance
of the same asymmetric dead-zone bug the grid side was fixed for above -- `KorenTriode`/`KorenPentode` also
hard-zeroed a plate voltage's current AND gradient the moment it reached its own cathode (`vpk<=0`), and that zero
gradient is exactly what a near-singular Jacobian diagonal entry looks like. Fixed the same way (a smooth SoftPlus
floor, `TubeModels.h`'s `floorPlateVoltage()`), free everywhere except within a few volts of a plate crossing its
cathode -- normal operation never comes close.

**Two solver-level fixes were tried first and BOTH reverted for regressing the well-converging case** (independent
per-port step clamping; Levenberg-Marquardt diagonal damping, even tightly gated) -- see the linked doc for the exact
numbers. Neither is worth retrying without new evidence.

**Final numbers**, `BassmanHotInputProbe`/`BASSMAN_KNOBS_MAX=1`/`HM2StyleDistortion`, 30 s, full regression suite
passing throughout:
- Input = Normal (realistic playing): **8 -> 0 recoveries.**
- Input = Jumped, every control maxed (the most extreme corner): **167 -> 35 recoveries** (~4.8x fewer), all in the
  power block (`pre 0, power 133`), none left in the preamp.
Not fully zero at the most extreme corner -- an honest partial fix, same as the pass before it, but a substantially
smaller remaining gap and a real, general, project-wide correctness fix (every Koren triode/pentode benefits,
not just this amp) rather than a Bassman-specific patch.

## Cost
Per sample (48 kHz, one channel, dual-mono shortcut on): preamp ~3300 cycles, power section ~6500,
supply + housekeeping ~300. **18-29% of a core at 1x** (dev PC; it was 14-24% before page 2 added the speaker network, the power-drive nodes and the feedback pole). The
registry's tiers are 1x / 1x / 2x (eco / normal / high): the tubes' clipping is soft, so unlike the diode
pedals nothing measurable is gained at 2x for normal playing; "high" is there for hard-driven presence.
This is about 3x the diode pedals' 5% target and is the honest price of eight physically modelled tubes,
a transformer and a global feedback loop in one netlist. Measured breakdown of the Newton work before the tables went in:
device evaluation 38%, the dense port solve 23%, Jacobian/error bookkeeping 38%. What was tried:
* Hermite tables instead of exp/log/pow: 20% -> 14%.
* A second-order (quadratic) port predictor: -20% cost at low level, but a hard attack overshoots into a
  region Newton cannot recover from (output died at input 0.3): **rejected**; the linear predictor stays.
* Dropping the grid-cathode capacitances (5 capacitors).
Not done (backlog): channel-variant preamps (skip V1B/V1A while its volume is 0 -- one less triode, one
fewer port), block-Gauss-Seidel between phase inverter and output stage, running the preamp at a lower
rate, and the lane-parallel `SignalGraph` on the roadmap.

## Not modelled / known differences
* Output transformer: core saturation, real leakage/inductance (typical values); the speaker is a generic
  guitar speaker (one set of Re / Le / resonance scaled to 4 / 8 / 16 ohm, not a specific driver). A cabinet after
  the amp supplies the speaker's acoustic response.
* Tubes: no microphonics, no heater or cathode-poisoning effects; grid-cathode / plate-cathode capacitances;
  tube-to-tube variation. The Koren 12AX7 is ~2 dB higher gain than Kuehnel's derived figures in the phase
  inverter (see table).
* Supply: no ripple/hum, rectifier conduction is averaged (a series resistance, 330 ohm chosen so the
  screen sag lands at -54 V against the published -57 V); the bias supply is the 15k || 56k with an 8 uF
  capacitor, its rectifier is an ideal -48 V source; ac line variation and the standby switch are not
  modelled.
* Input jack 2, the input 1M resistors, and the ground / AC / standby switches.
* The cathode follower V2B is an ideal follower (gain 0.984, output impedance 531 ohm applied outside
  the block): it does not clip.
* `Output` and the fixed scale `outputScale = 1/20` (a 13.4 V peak, 45 W speaker signal maps to 0.67 of
  full scale) are plug-in conveniences; the registry trims the noon setting to unity for the
  reference guitar signal (-7.70 dB, `docs/circuits/UnityLevel.md`).

## The below-cathode plate bug, closed (2026-09-26): probably the "picotando" of the HM-2 into the Bassman
Found while fixing the Super Lead's glitching at high gain (`SuperLead1959.md`, "The glitching at high gain"): `KorenTriode::evaluate()` applied the plate-voltage floor twice, so a plate BELOW its
cathode still passed the current of a plate at 1.39 V whenever the grid was positive (0.5-3.7 mA down to -180 V). That phantom conduction is a second, non-physical root for Newton in the
phase inverter, and `NodalCircuit` now also refuses to accept a "converged" point whose port equations are more than 30 V out. Measured on this amp's own hot-pedal test (10 s, 3 V square-ish notes):
**sanity rejects 22 -> 0, worst speaker volts 136 -> 54, restores 0**. Everything above about "still open" is superseded for this cause; only listening on the host can say whether the audible
glitch is gone.
