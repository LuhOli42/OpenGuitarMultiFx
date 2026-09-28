# Twin Reverb-Style Amplifier (Fender Twin Reverb, blackface "AB763")

Display name **"Twin Reverb-Style Amplifier"**. Runs on [`NodalCircuit`](./NodalCircuitSolver.md), same three-part
structure (preamp, power block, supply model) as the Bassman/Super Lead.

## Source
Fender's own factory schematic, part 045377, "TWIN REVERB-AMP AB763" (fetched via `el34world.com/charts/Schematics/files/
fender/Fender_twin_reverb_ab763_schematic.pdf`, cross-checked against a second copy at `schematicheaven.net`). Read directly
from the rasterized drawing at 300 dpi, region by region -- this drawing is unusually generous with printed DC voltages
(plates, screens, phase inverter, bias), which is what the DC-operating-point verification below is checked against.

## Scope: the clean amplification path only
This project's own "never model an effects loop" rule extends here: the built-in spring **reverb** (driver 12AT7 half +
transformer + tank + recovery 7025 half) and the bias-wiggle **vibrato/tremolo** oscillator are **not modelled**. This
project already has general-purpose `ReverbProcessor`/`TremoloProcessor` effects that cover that ground without
duplicating a real spring tank's own physics inside an amp model. What IS modelled: both channels' preamp + tone stack,
the shared gain-recovery stage, the long-tailed-pair phase inverter, the four 6L6GC output tubes, the output transformer
and the global negative feedback loop -- the same "whole clean amp" scope as the Bassman/Super Lead.

## What is modelled
1. **Preamp** (`ch.pre`): Normal and Vibrato channels are near-identical -- ONE 12AX7-family (7025) triode each (not two,
   unlike the Bassman/Super Lead's V1+V2 preamps), cathode-biased (1.5k + 25 uF, full bass, no differentiation between the
   two channels here), 100k plate load off a shared preamp B+ tap. Each channel then has its OWN complete Fender
   "blackface TMB" tone stack (a real structural difference from the Bassman/Super Lead, which share ONE stack after
   mixing) -- Treble 250k-A, Bass 250k-A (as a rheostat), Middle 10k-A (pot values are legible on the schematic; the
   250 pF/0.1 uF/0.047 uF cap values are the well-documented, cross-model-identical blackface constants, not individually
   legible at scan resolution -- this exact stack is used unchanged in the Deluxe Reverb/Pro Reverb/Vibrolux family too).
   Each channel's Volume (1M-A) wiper feeds a shared mixing node through 100k, the same "jumper cable" pattern as the
   Bassman/Super Lead's Normal/Bright channels.
2. **Power block** (`ch.power`): a 1/2 12AX7 gain-recovery stage (needed because two volume pots and a 100k summing
   resistor each lose real level), then a 12AX7 long-tailed-pair phase inverter (plate loads 82k/100k, printed; cathode
   tail 470 ohm into a 10k-to-feedback-node resistor, the same shape as the Bassman/Super Lead's own PI), then **four
   6L6GC as two push-pull PAIRS** (1500 ohm grid stoppers, printed; 220k grid leaks to a fixed -52 V bias rail, printed) --
   the exact same "2 tubes on the same nodes = one pentode with twice the current" pattern as the Super Lead's EL34 pairs,
   and the same tube COUNT (4) and pairing shape, which is what made this amp's power section fast to build. Output
   transformer, then a fixed (no Presence knob on this amp -- not on the real blackface front panel) 820 ohm feedback
   resistor from the speaker terminal into the PI's tail node.
3. **Supply**: rectifier + choke + 3 filter sections, sized to land the plate/screen rails at their PRINTED values
   (+460 V / +458 V) rather than assumed, since this schematic prints them.

## Assumptions and known differences from the drawing
* The 6L6GC pentode parameters are Koren's own PUBLISHED defaults (halved kg1/kg2 for the 2-tube pair, per the project's
  usual pairing recipe) -- NOT fitted against an external composite curve the way the Bassman's 5881s were, since no
  equivalent published curve was found for this specific circuit in the time available. Verified instead against the
  schematic's own printed DC operating point.
* Output transformer primary/secondary turns ratio is ASSUMED (not printed): sized for a ~2.6k plate-to-plate load (a
  reasonable value for 4x 6L6GC at 85 W) into a 4 ohm secondary (matching the real cab's two paralleled 8 ohm Jensens).
* Rectifier resistance, choke values, screen resistor and the phase-inverter/preamp idle-current constants are hand-tuned
  (same convention as the Bassman/Super Lead) so the DC solve lands near the schematic's own printed voltages: plate rail
  exact (460.0 V), screen rail close (458.5 vs 458 V printed), phase inverter plate A within the +-20% tolerance but not
  centred (288.6 V vs 245 V printed) -- flagged here rather than silently left; a future pass could tighten this by
  re-deriving `phaseInverterNodeCurrent` more carefully, but the amp is stable and level-correct as measured below.
* The real amp's Input is two independent full-featured channels (each with its own Volume/Treble/Middle/Bass); this
  model exposes ONE shared knob set (Input: Normal / Vibrato / Both selects which channel(s) are driven, but the SAME
  knob values apply to whichever is active) -- a deliberate simplification since almost nobody plays both channels
  jumpered with genuinely different settings. Page 2 (Power Drive, Bias, Tube Feel, Speaker) matches every other
  modelled amp in this project's own established convention, none of which exist as distinct controls on the real
  amp's front panel (same as the Bassman/Super Lead's own page 2).

## Verification (`Tests/TwinReverbStyleAmplifierProcessorTests.cpp`)
DC: plate rail 460.0 V (exact), screen rail 458.5 V (printed 458), phase inverter plate A 288.6 V (printed 245, within
the +-20% tolerance). Silence settles with zero drift/failures. A plucked note through each Input setting (Normal /
Vibrato / Both) stays finite and bounded with a failure rate under 2e-3. Page 2 has exactly 4 parameters (Power Drive,
Bias, Tube Feel, Speaker).

## Reduced-order (behavioural) power stage -- the shipped default
Built in FROM THE START this time (per the user's own request, 2026-09-28: "pros proximos modelos vc calcula certinho a
tabela e faz"), not bolted on after like the Bassman/Super Lead were -- same mechanism, same user-approved exception to
circuit fidelity; see `docs/circuits/SuperLead1959.md`'s own "Reduced-order (behavioural) power stage" section for the
full mechanism and reasoning, only what's specific to this amp is repeated here.
* **Boundary**: BOTH channels' preamp + tone stack (`ch.pre`, a real linear/triode circuit, unconditionally solved) stay
  exact; the gain-recovery stage, phase inverter, four 6L6GC, output transformer, feedback and physical speaker
  (everything in `ch.power`) are replaced by `behavioralPowerStage()`. In `reducedOrder` mode, `ch.power` is left
  entirely EMPTY (no nodes, no devices at all) -- `prepare()`/`process()` skip calling it outright rather than solving a
  circuit with nothing in it.
* **Calibration** (`TR_POWERCAL` in the test file): drive point is `pMix` directly (there is no separate `toneStackOut`
  concept here since the tone stacks live upstream of the mix, unlike the Bassman/Super Lead) -- 20 points from deep
  small-signal to full saturation, Input Vibrato, Volume 0.8, Power Drive max, 1.5 s settle per level. Output plateaus
  cleanly around 37.2-37.9 V once the rail sags into the 394-398 V range (the guitar-input sweep's own top few levels
  exceed `inputLimit()`'s ~0.65 V ceiling, so the last swept level was dropped from the sag table -- redundant with the
  point just before it, which had already reached the plateau).
* **Fitted constants**: `bmGain0 = 62.5` (small-signal gain, pMix -> speaker), `bmYmax = 0.096` (peak output as a
  fraction of the sagged rail), `bmKneeN = 6` (same knee sharpness as the other two amps -- worked well here too, no
  retuning needed), `bmShelfHz = 90`, `bmShelfHfGain = 0.7` (a first estimate, then verified/adjusted purely via the
  registry trim below rather than needing a shelf retune -- the level match came out this good without one).
* **Verified**: level tracks the reference within **0.01-0.06 dB** across a 5e-3 to 0.6 V sweep -- the tightest match of
  any amp on this pattern so far (better than the Super Lead's 0.04-0.16 dB and the Bassman's 0.01-0.33 dB), likely
  because this amp's `ch.power` in reduced mode is trivially empty rather than a partial linear remainder, so there is
  nothing left to interact with the fitted curve except the (already-exact) preamp/tone-stack signal. `PedalUnityLevelTests`
  passes at 0.00 dB (registry trim `+4.24f`, re-measured directly against the shipped reducedOrder default, not carried
  over from a stale reference-only number). Worst-block cost under the same hot-pedal stress the other amps were
  measured against: **4.07% avg, 5.9% worst block, zero failures/recoveries by construction** -- the cheapest of the
  three amps on this pattern (an empty `ch.power` costs strictly less than even a tone-stack-only one).
* **Speaker (4/8/16 ohm)**: bit-identical across all three settings (the same `speakerGain = 1.0` unconditional fix as
  the Super Lead/Bassman -- there is no physical speaker mismatch left to compensate for in this mode).
* **Known, documented gaps** (same list as the other two amps): Presence doesn't exist on this amp regardless of mode
  (not on the real front panel); Bias and Tube Feel have no effect in reducedOrder (they only ever moved nodes that no
  longer exist); the speaker's own resonance/HF lift is not reproduced.

## Cost
Full reference netlist: not separately profiled (reducedOrder shipped from the start, per the user's request, rather than
measuring a reference-netlist cost baseline first the way the Bassman/Super Lead's initial builds did). **The shipped
default (reducedOrder) is ~4.07% average, ~5.9% worst block, zero failures.** Orders {0, 0, 1} (2x only at High), same
tier convention as the other two amps. Unity trim +4.24 dB.
