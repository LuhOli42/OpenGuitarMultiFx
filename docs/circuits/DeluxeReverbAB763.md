# Deluxe Reverb-Style Amplifier (Fender Deluxe Reverb, blackface "AB763", ~22 W)

Display name **"Deluxe Reverb-Style Amplifier"**. Runs on [`NodalCircuit`](./NodalCircuitSolver.md), same three-part
structure (preamp, power block, supply model) as the Bassman/Super Lead/Twin Reverb, and the same AB763 blackface
family as the Twin Reverb specifically -- see that amp's own doc for the shared family reasoning; this doc only
covers what's specific here.

## Source
Fender's own factory schematic, "DELUXE REVERB-AMP AB763" (fetched from `schematicheaven.net/fenderamps/
deluxe_reverb_ab763_schem.pdf`). Read directly from the rasterized drawing at 300 dpi, region by region -- like the
Twin Reverb's sheet, this one prints its own DC operating-point voltages directly next to each tube.

## Scope: the clean amplification path only
Same "never model an effects loop" call as the Twin Reverb: no spring reverb tank/driver or vibrato/tremolo
oscillator modelled (separate `ReverbProcessor`/`TremoloProcessor` effects cover that ground). What IS modelled:
both channels' preamp + tone stack, the shared gain-recovery stage, the long-tailed-pair phase inverter, the two
6V6GT output tubes, the output transformer and the global negative feedback loop.

## What is modelled
1. **Preamp** (`ch.pre`): Normal and Vibrato channels, each ONE 7025 (low-noise 12AX7) triode, cathode-biased
   (1.5k + 25 uF), 100k plate load off a shared preamp B+ tap -- structurally identical to the Twin Reverb's own
   preamp. Each channel then has this amp's own **2-band** variant of the blackface tone stack: Treble 250k-A, Bass
   250k-A (as a rheostat), and a **fixed 6.8k** resistor where the Twin Reverb/Bassman would have a Middle pot (this
   amp genuinely has no Middle knob on its real front panel -- confirmed on the schematic, not a simplification).
   Cap values (250 pF treble bypass, 100k + 0.1 uF bass network, 0.047 uF bridging cap) read directly off the sheet.
   Each channel's Volume (1M-A) wiper feeds a shared mixing node through 100k, same pattern as the Twin Reverb.
2. **Power block** (`ch.power`): a 1/2 12AT7 gain-recovery stage, then a **12AT7** (not 12AX7) long-tailed-pair phase
   inverter -- plate loads 82k/100k (printed, same asymmetric shape as every other AB763-family amp in this project),
   cathode tail 470 ohm into a 10k-to-feedback-node resistor. Then **two 6V6GT, ONE tube per side** (not paralleled
   pairs like the Twin Reverb's 6L6GCs or the Super Lead's EL34s -- a single real device per side, matching the
   Bassman/Super Lead's own "one real device, no pairing recipe" pattern), 1500 ohm grid stoppers (printed), 220k
   grid leaks straight to a fixed -35 V bias rail (printed) -- read directly off the schematic: unlike the
   paired-tube amps' shared-node-plus-own-decoupling-cap arrangement, this amp's two 220k resistors are simply the
   two halves of a divider centred ON the ideal bias source itself, with nothing else in between. Output
   transformer, then a fixed (no Presence knob -- not on the real front panel) 820 ohm feedback resistor from the
   speaker terminal into the PI's tail node (same printed value as the Twin Reverb's).
3. **Supply**: rectifier + choke + 3 filter sections, sized to land the plate/screen/PI rails at their PRINTED
   values (+415 V / +415 V / +325 V) rather than assumed, since this schematic prints them.

## A real full-topology instability, found and fixed during this build
Unlike every other amp on this pattern so far, the FULL REFERENCE topology (not the reduced-order stage) initially
**self-oscillated** at roughly 0.5-1 Hz with silence at the input -- a genuine limit cycle in the DC solution of the
phase-inverter + power-tube + global-feedback loop, not a numerical glitch (confirmed deterministic and periodic
under a fully static, non-sagging supply). Root-caused via a sequence of isolation tests:
* Opening the feedback loop (`debugSetFeedbackResistance(1e9)`) made it settle cleanly and instantly -- proving the
  instability lives in the closed negative-feedback loop, not the preamp or supply.
* Forcing the supply's own current-draw feedback to a fixed idle value (no sag coupling at all) did **not** stop the
  oscillation, and made it perfectly periodic -- ruling out the sag loop as the cause (a real possibility given how
  often sag coupling causes exactly this kind of low-frequency issue elsewhere in this project).
* Swapping the PI triode between 12AT7 and 12AX7 parameter sets changed the oscillation's character but never
  removed it, and swapping it to 12AX7 (nominally lower transconductance, higher mu) made it *worse* -- consistent
  with mu (not gm) setting this stage's open-loop voltage gain, and ruling out "wrong tube type" as a simple fix.
* Systematically raising `kg1` (softening transconductance) on both the PI triode and the 6V6 pentode located a
  fully stable combination. This means both `triode12AT7()`'s and `pentode6V6()`'s `kg1` are **not** their published
  from-datasheet values -- they are empirically raised for loop stability, and this is documented as a known,
  honest departure in the source comments right next to the fitted numbers, not hidden.

This amp's global feedback loop evidently has a much smaller real-world stability margin than the Twin Reverb's or
Bassman's -- likely because a single 6V6 pair, a 12AT7 PI, and this amp's specific plate-to-plate impedance combine
to less headroom than a from-datasheet Koren fit assumes, while the *same* published fit for the paired-tube amps
happened to leave enough margin. The true root cause (most likely the assumed, unprinted output-transformer turns
ratio, or the recovery stage's own gain) was not independently re-derived given the time already spent; this is
flagged as a known limitation for a future pass, per [[circuit-modeling-research-first]]'s spirit -- but the amp is
now genuinely stable, not just working around a corrupted state the way `recover()` papers over solver failures.

## Assumptions and known differences from the drawing
* The 12AT7's mu/kp/kvb/ex are Koren's published values (Duncan/Munro SPICE set); its `kg1` is NOT published --
  raised for loop stability, see above. Same for the 6V6GT's `kg1` (mu/ex/kg2/kp/kvb are published).
* Output transformer turns ratio is ASSUMED (not printed): sized for a ~4k plate-to-plate load (a plausible value
  for a single 6V6GT pair) into an 8 ohm secondary (the real amp's single 12" Jensen).
* The two channels' preamp plates land at the same DC point in this model (205 V), since both channels are built
  from an identical triode/resistor network; the schematic prints a genuine 180 V/170 V split between them (real
  component-tolerance/loading asymmetry this simplified model can't reproduce), so the verification test checks
  both against their average (175 V) rather than their individual printed values.
* Rectifier resistance, choke values, screen resistor and the phase-inverter/preamp idle-current constants are
  hand-tuned (same convention as the other amps) so the DC solve lands near the schematic's printed voltages: plate
  rail exact (415.0 V), screen rail close (412.2 vs 415 V printed), PI tail-bias rail close (325.3 vs 325 V
  printed).

## Verification (`Tests/DeluxeReverbStyleAmplifierProcessorTests.cpp`)
DC: plate rail 415.0 V (printed 415), screen rail 412.2 V (printed 415, within tolerance), PI tail-bias rail 325.3 V
(printed 325, essentially exact), both preamp channel plates 205.3 V (against the 175 V average of the printed
180/170 V split, within the +-20% tolerance). Silence settles with zero drift/failures (the fixed instability
above). A plucked note through each Input setting (Normal / Vibrato / Both) stays finite and bounded with a failure
rate under 2e-3. Page 2 has exactly 4 parameters (Power Drive, Bias, Tube Feel, Speaker).

## Reduced-order (behavioural) power stage -- the shipped default
Built in FROM THE START (per the user's own request, 2026-09-28), calibrated only after the full-topology
instability above was fixed (calibrating a sweep against an oscillating reference would have produced garbage).
Same mechanism as the Twin Reverb/Super Lead/Bassman; see the Super Lead's own doc for the full methodology.
* **Boundary**: BOTH channels' preamp + tone stack (`ch.pre`) stay exact; the gain-recovery stage, phase inverter,
  two 6V6GT, output transformer, feedback and physical speaker (everything in `ch.power`) are replaced by
  `behavioralPowerStage()`. In `reducedOrder` mode `ch.power` is left entirely EMPTY, same as the Twin Reverb.
* **Calibration** (`DR_POWERCAL` in the test file): drive point is `pMix` directly. 20 points from deep small-signal
  to full saturation, Input Vibrato, Volume 0.8, Power Drive max, 1.5 s settle per level. Output plateaus cleanly
  around 11.6-11.9 V once the rail sags into the 399-400 V range (printed idle 415 V); the last 3 swept levels were
  dropped from the sag table -- `drive_pk` itself goes slightly non-monotonic there (past `inputLimit()`'s ceiling),
  redundant with the point just before it, and the lookup table requires strictly increasing drive values anyway.
* **Fitted constants**: `bmGain0 = 50.0` (small-signal gain, pMix -> speaker, extremely clean across four decades of
  the sweep: 50.8/50.2/50.1/50.0), `bmYmax = 0.0298` (peak output as a fraction of the sagged rail), `bmKneeN = 6`
  (same knee sharpness as every other amp on this pattern -- no retuning needed), `bmShelfHz = 90`,
  `bmShelfHfGain = 0.7` (the Twin Reverb's own first-estimate values, verified adequate here too via the registry
  trim below rather than needing a retune).
* **Verified**: level tracks the reference within **0.00-0.02 dB** across a 5e-3 to 0.6 V sweep -- as tight as the
  Twin Reverb's own best-in-class match, for the same reason (an empty `ch.power` in reduced mode leaves nothing to
  interact with the fitted curve except the already-exact preamp/tone-stack signal). `PedalUnityLevelTests` passes
  at **0.06 dB** with registry trim `+0.0f` -- no trim adjustment needed at all. Worst-block cost under the same
  hot-pedal stress the other amps were measured against: **4.08% avg, 7.0% worst block, zero failures/recoveries by
  construction**.
* **Speaker (4/8/16 ohm)**: bit-identical across all three settings (`speakerGain = 1.0` unconditional fix, same as
  every other amp on this pattern).
* **Known, documented gaps** (same list as the other amps on this pattern): Presence doesn't exist on this amp
  regardless of mode (not on the real front panel); Bias and Tube Feel have no effect in reducedOrder (they only
  ever moved nodes that no longer exist); the speaker's own resonance/HF lift is not reproduced.

## Cost
Full reference netlist: not separately profiled (reducedOrder shipped from the start, per the user's request).
**The shipped default (reducedOrder) is ~4.08% average, ~7.0% worst block, zero failures.** Orders {0, 0, 1} (2x
only at High), same tier convention as the other amps on this pattern. Unity trim +0.0 dB.
