# Super Lead-Style Amplifier (Marshall 1959 Super Lead, 100 W "plexi")

Display name **"Super Lead-Style Amplifier"** (category Modeled Amps), registry key `SuperLeadStyleAmplifier`. Runs on
[`NodalCircuit`](./NodalCircuitSolver.md) exactly like the [Bassman-style amplifier](Bassman5F6A.md), and shares its machinery
(`Source/Effects/TubeAmpCommon.h`: speaker model, input protection, de-click, cathode-follower numbers). Code:
`Source/Effects/SuperLeadStyleAmplifierProcessor.{h,cpp}`, tests `Tests/SuperLeadStyleAmplifierProcessorTests.cpp`.
(The amp is a Marshall product; the "-Style" convention applies, see `PositiveGroundBooster.md`.)

**Which amp**: the 100 W 1959 Super Lead (four EL34, two ECC83 gain stages, an ECC83 phase inverter). The 50 W 1987 shares the
preamp, tone stack and phase inverter and differs in the power section (two EL34, another transformer); it is not modelled.

## Sources
Marshall's own drawings of the **1959SLP reissue**, from schematicheaven.net `marshallamps/slp_reissue_100w_1959.pdf`:
* **1959-01-60-02, issue 2, 2002** ("Circuit diag.", the whole amp on one sheet): every value used below is printed on it. **This is the
  reference.** It prints **no voltages**.
* **59X-60-02, issue 7, 1993** (the "Lead preamp section"): older, with more part designators. It agrees on V1 and V2's cathode
  networks, the 100k plate loads, and the 470k mixing; it differs in the bright channel (C17 4n7 + C18 1n across the Loudness pot and C4 470p +
  C5 120p to V2's grid, against the 2002 drawing's 4n7 and a single 470p) and in the tone stack's values (220k / 22k / 220 pF against 250k /
  25k / 470 pF). **The 2002 values are used.**
Also read: the 1967 hand drawing ("100 Watt Amplifier Type #2", `jmp_superlead_100w_1959.pdf`), the only one with voltages (270 / 300 / 375 /
460 V) and a choke value (20 H); and the 1970 Unicord "1959 Mark II" (6550s, US distribution). Neither is modelled.

## What is modelled (2002 drawing)
```
guitar -> [pre block]  68k stopper -> V1A (Channel II / Normal: 820 || 330 uF cathode, 100k plate, 22 nF out) or
                                      V1B (Channel I / Bright: 820 || 680 nF cathode, 100k plate, 3n3 out, 4n7 across the pot)
                       -> Loudness pots (1M audio) -> 470k mixing resistors (Bright also 470 pF) into V2A's grid
                       -> V2A (820 || 680 nF, 100k plate) -> V2B cathode follower (an ideal follower with the DC drop of the real one)
       [power block]   -> 531 R (follower output impedance) -> Marshall TMB stack: 470 pF, 33k, 250k treble, 1M bass (a rheostat),
                       25k middle (tapped), 2 x 22 nF -> 22 nF -> V3 long-tailed pair (82k / 100k plate loads, 47 pF between plates)
                          cathodes -> R17 470 -> M (the 1M grid leaks meet here) -> R20 10k -> F (the feedback node)
                       -> 2 x 22 nF -> 2 x 220k grid leaks to the bias node -> two EL34 PAIRS (750 R grid stoppers, 500 R screen resistors)
                       -> output transformer (~1.7k plate to plate, 16 ohm tap) -> 16 ohm speaker
                       <- R21 47k from the 16 ohm terminal into F; F to ground through the 5k Presence pot with 100 nF across its top segment;
                          100 nF from F into V3's second grid
       [bias supply]   an ideal rectified winding behind R29 15k, C17 10 uF, R28 56k + the 20k trimmer to ground: the grids' return
       [supply model]  bridge + transformer resistance -> 50 uF (plates, before the choke) -> choke -> 50 uF (screens, after it, with the
                       112k of equalising resistors) -> R26 + R27 20k -> 100 uF (phase inverter) -> R15 10k -> 50 uF (V2) -> R14 10k -> 50 uF (V1)
```
Controls (0..1), **page 1**: Input (Normal / Jumped / Bright: which channel's jack(s) the guitar is in; **the default is Normal**, see
"Known limits"), Loudness II (the normal channel), Loudness I (the bright channel), Treble, Middle, Bass, Presence, Output (a plug-in level control;
the real amp has no master volume). **Page 2**: Power Drive (a master volume in front of the tone stack), Bias (the drawing's 20k trimmer VR1),
Tube Feel (sag and feedback: 0 = stiff and solid-state-like, 1 = the real amp).
The second jack of each input is not modelled. (The effects loop is never modelled: the tone stack feeds the phase inverter directly.)

## Reading choices worth knowing
* **The Marshall stack is the Fender TMB with Marshall values** (33k slope resistor, 470 pF, 22 nF caps): a middle pot with a real wiper tap
  (C10 goes to the wiper) behaves the same as the usual rheostat except at the extreme; the noon insertion loss is **-8 dB at 1 kHz** (the
  Bassman's 56k makes it about -17 dB). Checked against an independent complex-analysis calculation of the same network.
* **The feedback node F carries three things at once**: R21 (the 47k from the speaker), the Presence network, and R20 (10k) into the phase
  inverter's cathode tail, plus a 100 nF into V3's second grid. Reading R17/R20 as the PI's tail (470 + 10k into F, which sits near ground) puts the
  cathodes at about 35 V, and the loop opens to +12 dB of gain against +48 dB closed, i.e. about 12 dB of feedback.
* **Two EL34s in parallel are one tube with twice the current** (kg1, kg2 halved, grid conductance doubled, the 1k5 stoppers and 1k screen
  resistors in parallel: 750 R and 500 R). Identical tubes on the same nodes are mathematically that; the cost is half.
* **Power stage cathodes are grounded** (fixed bias); the four screens hang on the post-choke node, the transformer centre tap on the
  pre-choke node.

## Assumptions (not on the drawings) -- change these first if the amp sounds wrong
| What | Value | Why |
|---|---|---|
| Plate rail | 480 V idle | a plexi's usual; the drawing prints nothing. The other rails come out of the model's own currents: **screens 478, PI 330, V2 281, V1 259 V** (the 1967 drawing's marks are 460 / 375 / 300 / 270) |
| Supply resistance | 150 R (bridge + transformer), choke 80 R / 20 H | sags the plates to ~415 V at 105 W; the 20 H is the 1967 drawing's |
| Output transformer | 1.7 kOhm plate to plate, 10.6 H, 16 ohm tap, k 0.9997 / 0.9992 | two EL34 pairs each want ~3.4k; not on the drawing |
| Bias supply | -67.5 V rectified winding | puts the trimmer at noon at -55 V (35 mA per EL34 here). The trimmer only spans about -53.5 to -56.5 V because it is in series with a 56k **loading** a 15k source: that is the drawing |
| EL34 | Koren, fitted: mu 8.11, Kp 100, Ex 1.5, Kg1 1201, Kg2 3720 (per tube) | Koren's published set (mu 11, Kp 60) cuts off near -40 V at 480 V where a plexi is biased at -55 V. Fitted to 36 mA at -55 V / 480 V, gm 6.6 mA/V there, ~160 mA at 250 V / -13.5 V. The datasheet's 100 mA at that point is not met (the amp lives at 480 V) |
| Speaker | 4 / 8 / 16 ohm (default 16), Re 6.5 z, Le 0.55 mH z, cone resonance 85 Hz, **eddy loss across Le of 9.4 x the nominal impedance** | the speaker load is switched on the fixed 16 ohm tap, exactly as the Bassman switches it on its 8 ohm tap (a 4 / 8 ohm cab is a mismatched load); the real amp's impedance selector is not modelled |
| Transformer losses | 20k across the primary, 400 pF plate-to-plate and to ground, **a 2k + 3 nF series R-C across the primary** | dielectric / eddy losses at 10-20 kHz; without them the speaker's inductance rings against the winding capacitance |
| Pot laws / directions | Loudness and Bass audio, Treble / Middle / Presence linear; clockwise = more (Presence: the wiper-to-ground segment shrinks) | the tone directions were checked on the network |

## Verification (`Tests/SuperLeadStyleAmplifierProcessorTests.cpp`)
* Operating points sane (rails in a decreasing chain; **35 mA per EL34 idle, 3.5 mA screens**); bias node -55.0 V; no drift over 1 s of silence.
* Bias trimmer: idle 187 / 138 / 107 mA (four tubes) at knob 0 / 0.5 / 1.
* Tone stack: each control acts in its band and not in the others; -8.2 dB at noon.
* Channels: Channel I has 36 dB more 4 kHz-to-100 Hz than Channel II at the mixing node; the input selector's blocked channel passes <1%.
* **Power: 105 W at 3.3% THD, 132 W at 6.9%** (1 kHz into 16 ohm; the sweep crosses 5% between them), plate rail 480 -> 415 V at 105 W. The Marshall spec is 100 W.
* Global feedback: 47.9 dB closed vs 59.8 dB open; Presence +6.6 vs -7.0 dB at 6 kHz relative to 500 Hz.
* Solver: single-channel playing (Input = Normal or Bright) never fails in a 10 s hot-pedal run (3 V square-ish notes); random knob combinations
  and a 10 s soak of plucked notes: 0 failed solves, 0 restores.

## The glitching at high gain: what it was, and what fixed it (2026-09-26)
User: with Loudness I and II at maximum "o som começa a bugar, que nem com o HM-2 e o Bassman". Reproduced with plucked notes and with a hot pedal (`SL_SOAK`, `SL_HOT_RATE`,
`SL_EXPLORE_HOT` in the tests): with Input = Jumped and a hot pedal, **144 sanity rejects, 9 failed solves and 3 restores per 10 s**; a captured event showed the phase inverter's plates
at -77 and -44 V and its tail 70 V too high inside one sample, then 6 kV on the power plates.
* **Root cause 1 (a real bug in `TubeModels.h`, shared by every tube circuit): a plate BELOW its cathode still conducted.** `KorenTriode::evaluate()` floored the plate voltage and
  then handed the floored value to `evaluateExact()`, which floors it again: two applications map any negative plate to 2 ln 2 = 1.39 V, not to ~0. So a triode with its grid driven
  positive passed 0.5 mA (grid +5 V) to 3.7 mA (grid +20 V) at *any* plate voltage down to -180 V. That phantom current is a second, non-physical root of the circuit: a phase inverter with both plates
  far below ground and the tail pulled up by their current, and Newton "converged" onto it. Fixed: the exact fallbacks now take the raw plate voltage. Regression test in the suite.
* **Root cause 2 (`NodalCircuit.h`): a converged point could still be far from balanced.** The "steps stopped shrinking" acceptance and a small node error on a device whose current is huge but
  insensitive (a plate arcing at 6 kV) could accept a state whose port equations are hundreds of volts out. A point is now accepted only if its port-equation error is under
  `portResidualAccept` (30 V: garbage is hundreds, a real point is micro-volts); otherwise the solver falls back as it does for any failure. (0.05 V was tried first and was far too strict: it rejected
  legitimate hard-driven points and made the solver spend 23 000 restores.)
* **Result** (10 s, 3 V square-ish notes into the amp): Input = Normal **0 rejects, 0 failed solves, 0 restores** (worst speaker peak 112 V, was 149); Input = Jumped **0 rejects, 12 held single samples, 0 restores** (was 144 / 9 / 3);
  soak of plucked notes with both Loudness at 1: 0 / 0 / 0. What helped earlier and stays: the speaker's eddy loss, the R-C across the primary, the 150 V sanity limit.
* Also tried and NOT what it was: sample rate, integration theta, feedback loop, grid conduction, screen dynamics, transformer coupling, tube input capacitances, EL34 knee.

## Known limits
* Input = Jumped with a hot pedal can still hold an isolated sample (a handful per 10 s, never a streak, no restores). **The default is Normal**, and the input protection (0.4 V knee, 0.65 V ceiling) is the Bassman's.
* The bench cost is the highest of any effect so far (below).
* The user's real target -- the amp pinned near a fixed ~20% and NEVER spiking higher when a boost pedal is added -- is **not met**. See "CPU: the real-time guard, tried and removed" below for what was tried and why a genuine worst-case spike (measured up to ~280% of one block's real-time budget with a TS808 in front) is still possible on rare, pathological samples. Lowering the average is only reachable structurally (half-rate preamp, sleep-on-silence, a reduced-order power stage), none of which are built yet.

## CPU: the real-time guard, tried and removed (2026-09-27)
User: "n quero que durante o show ele do nada tenha um pico de carga de cpu maior do que o limite e bugue o som" -- a wall-clock guard was added (if a block used more than `deadlineFraction` of its
real-time budget, the REST of that block solved at a hard `deadlineIterations`-iteration cap) and then REMOVED the same day once measurements ruled it out:
* **It fired on ordinary hot playing, not just emergencies.** The worst *reference* block (no guard) already costs ~37-68% of the block's real-time budget depending on the scenario -- close to
  or above the guard's own 55% trigger -- so a normal hot pedal into the amp tripped it routinely, not just in a genuine overrun.
* **Once it fired, a 3-iteration cap was not enough to keep the power stage sane.** The "hot pedal into ONE channel" regression test (previously 0 recoveries) went to **500-836 recoveries and a
  0.4-19% failure rate** with the guard active, confirmed by disabling the guard alone (nothing else changed) and watching the same test go back to 0/0. This is the same conclusion the earlier
  forced-`SL_BUDGET` experiment reached (see below): a low, fixed Newton-iteration ceiling degrades quality WORSE than doing nothing, because it is applied to every sample in the block regardless
  of whether that particular sample needed it.
* Removed the whole mechanism from `SuperLeadStyleAmplifierProcessor` (the wall-clock check, `deadlineFraction`/`deadlineIterations`/`deadlineBlockCount`, the `SL_BUDGET` dev test). `NodalCircuit`'s
  `setDeadlineMode()` plumbing is left in place (harmless, unused, no other processor calls it) rather than touched again.
* **What stayed, and is a genuine improvement**: the cheap `DynamicState` recovery (added the same week to cut `recover()`'s cost) had its own bug fixed alongside this -- it only ever saved the
  snapshot once, at `prepare()` (the amp's silent idle point), so every recovery mid-note reset the amp to silence instead of to what it was actually doing. Now refreshed every `restRefreshInterval`
  (512 samples, ~10.7 ms) while the channel is converging cleanly, in both this amp and the Bassman. This is what actually made "hot pedal into ONE channel" pass again after the guard was removed.
* **A second, narrower lever that WAS kept**: `NodalCircuit`'s mode-2 (last-resort quarter-step) Newton ceiling was cut from 900 to 300 iterations/sample. Evidence first (`SL_ITERHIST`, a dev-only
  histogram of `debugLastPowerIterations()` over 4 s of a hot-pedal signal): out of 192 000 samples, the overwhelming majority need under 5 iterations, but a real tail exists -- 136 need 50-99, 5
  need 100-199, and 31 hit the old 900 ceiling's neighbourhood outright. Cutting to 300 bounds that tail's absolute worst case 3x with **zero regressions on the full test suite** (every processor,
  not just this amp). A further cut to 100 WAS tried and reverted: it turned 0 recoveries into 84 on the "hot pedal into ONE channel / Bright" test -- some of that tail genuinely needs more than
  100 iterations to converge, so 100 traded correctness for a CPU bound this circuit doesn't actually need. 300 is the number to keep unless new evidence says otherwise.
* **What this left unresolved at the time**: even with all of the above, `PresetChainBench` on the user's own TS808-into-Super-Lead preset still showed the power block occasionally costing up to
  ~250-280% of a block's real-time budget for a single rare block. That block was not failing/recovering -- it was many individually-expensive-but-correctly-converging samples landing in the same
  128-sample window. **Superseded by the behavioural power stage below**, which removes the Newton solve (and so the tail) from the power stage entirely.

## Reduced-order (behavioural) power stage -- the shipped default (2026-09-27)
User, after the above still left an unbounded worst-case block: "eu acho q o q devemos fazer e garantir q o amp seja 20% de processamento e ele n suba... n quero q durante o show ele do nada tenha
um pico de carga de cpu"; then, once told the honest cause was intrinsic to solving an implicit nonlinear network (variable Newton iteration count is not this project's bug, every SPICE-like solver
has it), explicitly accepted trading circuit fidelity for a fixed-cost model in the power stage specifically ("exceção consciente"), same category of exception as `HM2StyleDistortion.md`'s
reducedOrder but a much larger cut. **`SuperLeadStyleAmplifierProcessor::reducedOrder`** (class default `false` -- every other test in `Tests/SuperLeadStyleAmplifierProcessorTests.cpp` assumes the
full topology; **turned on for the real app in `EffectRegistry.cpp`'s factory**, after the user listened to it against their own TS808+Super Lead preset and approved it):
* **What stays real, unconditionally**: the preamp (both triodes, the mixing node, the cathode follower) and the **tone stack** (Treble/Middle/Bass) -- a genuine linear circuit either way, so it
  costs nothing extra to keep solving exactly. `toneStackOut` is the boundary.
* **What is replaced**: everything from the tone stack's output onward -- the phase inverter, the two EL34 pentode pairs, the output transformer, the global negative feedback loop, and the
  physical speaker RLC network. `behavioralPowerStage()` computes the speaker-equivalent voltage directly in C++: a saturating curve `y = bmYmax * u / (1 + u^bmKneeN)^(1/bmKneeN)` (u = drive / knee,
  knee scales with the current rail) plus a slow envelope-follower-driven sag lookup (a real 20-point table, not a fitted shape -- the sag curve's rise was too irregular to fit cleanly), replacing
  the coupled tube/transformer network with a fixed handful of multiplies and one `pow()` -- genuinely CONSTANT cost, no Newton iteration to have a bad day.
* **Root cause, found before building this**: opening the global feedback loop on the FULL reference model did NOT remove the expensive tail (it got slightly worse) -- ruling out the phase
  inverter/feedback coupling and pointing at the power pentodes' own device dynamics under extreme drive as the actual source (consistent with `NodalCircuitSolver.md`'s "near-singular-Jacobian
  corner", a previously-flagged, not-fully-resolved lead in this exact area).
* **How the calibration was done** (`SL_POWERCAL` in the test file): the full reference model, driven with a slow settled sine at 20 levels from deep small-signal to full saturation, Power Drive at
  max, recording (`toneStackOut` peak, `speaker` peak/rms, plate rail) once the supply reached ITS OWN quasi-equilibrium at each level -- so supply sag is captured as real measured data, not assumed.
  One point is a direct cross-check: at drive_pk 6.238 V, the model's own 40.99 V rms speaker output is EXACTLY the doc's earlier "105 W at 3.3% THD" figure (40.99^2 / 16 = 105.0 W).
* **Fitted constants**: `bmGain0 = 9.5` (closed-loop small-signal gain), `bmYmax = 0.198` (peak output as a fraction of the rail at full saturation), `bmKneeN = 6` (knee sharpness) -- found by hand
  against the normalized (drive/rail, output/rail) calibration curve, not guessed; residual under ~2% (~0.2 dB) everywhere checked.
* **Verified** (`Tests/SuperLeadStyleAmplifierProcessorTests.cpp`, permanent, not dev-only): level tracks the reference within **0.04-0.16 dB** across the whole swept range (small-signal to full
  clip) -- better than the BD-2 macro's own 0.27-0.81 dB precedent. Worst-block cost on the same hot-pedal stress that used to hit 68%/280%: **13.7-15.8% worst block** (was up to 280%), average
  **7.7-8.2%** (was ~21-24%). Zero failures, zero recoveries, by construction -- mathematically impossible, since there is no Newton solve left in the power stage to fail.
* **Real bug found and fixed by the user listening (2026-09-27)**: the Speaker (4/8/16 ohm) level-matching factor (`speakerGain`, a `z^-0.8` law) was originally FITTED to cancel a real physical
  level difference that exists in the full reference netlist's mismatched-tap model (verified: 48.3/51.7/49.1 V rms full-drive there, within ~1 dB). reducedOrder has no such physical mismatch left
  to cancel (the calibration doesn't vary with speaker choice at all) -- applying the SAME compensation factor there made 4 ohm ~9.6 dB louder than 16 ohm instead of matching them, the "salto de
  volume enorme" the user reported. Fixed: `speakerGain = 1.0` unconditionally in reducedOrder mode. Now bit-identical across all three settings (permanent regression test).
* **Known, documented gaps in this mode** (none of these existed before reducedOrder; a future pass could close them the same fitted-from-real-data way): Presence has no effect; Bias and Tube Feel
  have no effect (they only ever moved nodes that no longer exist); the speaker's own resonance/HF lift is not reproduced (level still matches across 4/8/16, tone doesn't change with the choice).
* **Second real gap found and fixed (2026-09-27, via `PedalUnityLevelTests`)**: the flat curve above has no frequency response of its own, but the real PI/OT/feedback loop it replaces does (Miller
  capacitances, the transformer's bandwidth, the loop's own frequency-dependent gain) -- measured directly against the reference at 100/165/500/1000/1650 Hz: gain is ~2-4 dB LOWER from 500 Hz-1.65 kHz
  than at 100 Hz there, while the flat curve is (by construction) identical at every frequency. Fixed approximately with a one-pole high-shelf cut on `behavioralPowerStage()`'s output
  (`bmShelfHz = 90`, `bmShelfHfGain = 0.55`, ~-5.2 dB above the shelf): a single shelf cannot reproduce the reference's actual shape (which dips through the mid-band and partially recovers by
  1.65 kHz -- more like a scoop than a monotonic rolloff), so real residual error remains at individual frequencies (measured: 100 Hz ~0.4 dB high, 165 Hz close, 500 Hz-1 kHz ~0.5-1.7 dB off, 1.65
  kHz ~2.9 dB low) -- closing this further needs a proper multi-band fit, not attempted yet.
* **Third finding, NOT a reducedOrder bug**: chasing the remaining gap in `PedalUnityLevelTests` (still ~3 dB after the shelf) found the test's generic "every page-1 knob at 0.5 (noon)" loop was
  landing the 3-way discrete Input selector (Normal/Jumped/Bright) on an ambiguous exact-midpoint rounding boundary (resolves to Jumped, not the documented default Normal) -- fixed in
  `Tests/PedalUnityLevelTests.cpp` to leave any stepped parameter (`range.interval >= 1`) at its own default instead. That same investigation also found the Super Lead's own registry trim had
  drifted out of calibration BEFORE any of this reducedOrder work (an earlier, unrelated fix earlier this session moved the reference's own small-signal gain ~2.76 dB): re-measured `-15.83f` ->
  `-18.99f` in `EffectRegistry.cpp`. **Verified**: `PedalUnityLevelTests` now passes at 0.00 dB (was 6.35 dB before any of these three fixes).
* This is the pattern to reuse for every future amp/heavy pedal on this solver, not a one-off: calibrate a fitted curve AND a shelf for the discarded stage's frequency response from the REAL
  reference model's own measured behaviour (never a guessed shape), keep the linear parts (tone stacks, filters) solving exactly since they cost nothing extra, verify level/cost with a permanent
  test AND `PedalUnityLevelTests` before it ships as the class default, and get an actual listen before flipping the registry-level switch.

## Cost
Preamp ~1.3 Newton iterations per sample, power block ~2.3 in the (still available, reference) full netlist -- **~20-29% of a core** there, worst block up to ~280% (see above). **The shipped
default (reducedOrder) is ~7.7-8.2% average, ~13.7-15.8% worst block, zero failures.** Orders {0, 0, 1} (2x only at High), as the Bassman. Unity trim -15.83 dB.
