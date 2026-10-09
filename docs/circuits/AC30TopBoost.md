# AC30-Style Amplifier (Vox AC30, "Top Boost" channel)

Display name **"AC30-Style Amplifier"** (category Modeled Amps), registry key `AC30StyleAmplifier`. Runs on
[`NodalCircuit`](./NodalCircuitSolver.md). Code: `Source/Effects/AC30StyleAmplifierProcessor.{h,cpp}`, tests
`Tests/AC30StyleAmplifierProcessorTests.cpp`.

Shares the [AC15](AC15Twin.md)'s own family traits (cathode-biased EL84 output tubes, no global negative feedback)
but scales up in every dimension: **four EL84s as two parallel pairs** (not two singles), a genuine **long-tailed-
pair phase inverter** (a single triode couldn't symmetrically drive four grids), and a newly-built two-gain-stage-
plus-cathode-follower preamp feeding the famous **"Top Boost" Treble/Bass tone stack** -- a bridged RC network, not
a simple TMB ladder.

## Source
Two matched factory sheets from `schematicheaven.net/voxamps/`: `topboost_preamp.pdf` (the Top Boost preamp) and
`ac30_poweramp.pdf` (the shared power section, rectifier, and supply). Both are clean CAD-quality drawings (not
faded hand-drawn scans like the AC15's own 1959 sheet), read directly at full resolution with high confidence.

## Scope: Top Boost channel only
The real amp's Normal channel (no Top Boost tone stack, mixed in ahead of the shared phase inverter) is not
modelled, matching this project's single-channel scoping for the JTM45/JCM800/AC15.

## What is modelled
```
guitar -> High or Low sensitivity jack (a plain input attenuation, both sharing V1a's grid)
       -> 68k grid stopper -> V1a (1k5 cathode + 25 uF bypass, 220k plate)
       -> 500pF/100pF coupling -> Volume (500k log)
       -> V2a (100k plate, 1k5 cathode + 25 uF bypass) -> DIRECT-COUPLED (a plain wire, confirmed on the drawing,
          no capacitor) -> V2b: an ideal cathode follower (56k cathode, unbypassed)
   [Top Boost tone stack, a bridged RC network confirmed on the drawing]
       -> IN -(50pF)-> A -(1M Treble, wiper=out)-> D; IN -(100k)-> B; B -(.022uF)-> D; B -(.022uF)-> E -> Bass
          pot's own wiper; Bass pot (1M) spans D..F; F -(10k)-> ground
   [power section, shared with the Normal channel on the real amp -- not itself channel-specific]
       -> long-tailed-pair phase inverter: TWO 12AX7 halves, shared 1k2 UNBYPASSED tail (confirmed); one grid
          driven from the tone stack through 0.01uF + 1M leak, the OTHER grid simply grounded through its own 1M
          leak -- this circuit has NO global feedback signal reaching it (confirmed absent on the drawing)
       -> each phase: 0.047uF + 220k grid leak into its own pair of EL84 grids (confirmed, symmetric)
       -> FOUR EL84s as two parallel pairs (one pair per phase), ALL FOUR sharing ONE cathode-bias node through
          their own 100 ohm resistors each (confirmed), self-biased (no fixed bias supply)
       -> output transformer, no negative feedback -> 15/8/0 ohm taps (the real amp's own printed values; this
          project's 4/8/16 Speaker convention rounds 15 to 16) -> speaker
   [supply] 5AR4 rectifier -> 20H/100mA choke (both confirmed) -> B+ (not printed, 400 V assumed -- a commonly-
          published AC30 Top Boost figure) -> a decoupled preamp tap (290 V, confirmed printed)
```
Controls (0..1), **page 1**: Input (High / Low sensitivity jack), Volume, Treble, Bass (the real amp's own Top Boost
controls), Output (a plug-in level control; the real amp has no master volume). **Page 2**: Power Drive (a synthetic
master ahead of the phase inverter, matching this project's convention), Bias (a synthetic shift of the shared
cathode-bias resistor -- the real amp has no adjustable bias trim, being self-biased), Tube Feel, Speaker (4/8/16
ohm, matching the real amp's own transformer taps almost exactly).

## Two reference-model bugs found 2026-10-05 (read this before the next section)
1. **Each EL84 plate was shunted to B+.** The "1k5 || 1k5 / 50 ohm" resistors were wired from the rail straight to the
   plate, i.e. 50 ohm in parallel with each half of the output-transformer primary. That shorted ~35 dB of the output
   stage away: the full reference made ~0.3 V rms at the speaker where a 30 W AC30 makes ~20 V rms. They are now in
   SERIES between each plate and its end of the primary (the same mistake was in the AC15, 100 ohm, fixed the same way).
2. **The 290 V preamp tap had no decoupling.** The model scaled the main B+ by 0.725 and fed it to the preamp directly,
   so all output-stage ripple reached the preamp unfiltered. With bug 1 fixed this closed a loop the real amp does not
   have: a ~235 Hz oscillation growing without bound in silence (vanished with the supply frozen; the speaker load made
   no difference). Now a first-order RC (22k + 16 uF assumed -- the printed 290 V is legible, the parts are not).

Consequences: the old `bmGain0 = 0.00128` / `bmYmax = 0.00105` (and the +25.88 dB trim) were fitted to the broken
reference -- the cause of the "AC30 is too quiet" report. Refitted: `bmGain0 = 17.3`, `bmYmax = 0.0583`; trim -11.63 dB.
With both bugs fixed the amp is stable even at the published EL84/12AX7 kg1, but then delivers ~42 V rms into 16 ohm
(~110 W, impossible for four EL84s); the 6x/6x kg1 split below gives ~18 V rms (~20 W), so it stays -- now as the
closer match to the real output power, not as a stability fix. Also fixed: every amp's silence test cleared its buffer
only once, so each block's OUTPUT was fed back in as the next block's input (harmless while the reference was nearly
mute, a self-made feedback loop once it was not).

## A small power-stage instability, and its fix (2026-09-29 -- superseded, see above)
Unlike the AC15 (which settled cleanly on the first attempt), this circuit's full-topology reference model showed a
genuine, if small, self-oscillation with silence at the input: the LTP settled into a slow (multi-second period),
low-amplitude flip-flop between two nearly-symmetric plate states instead of a single fixed point (peak ~0.047,
against this project's usual <0.01 bar over a 10 s soak).

**What was ruled out**: freezing the supply's own dynamic current-draw feedback (`debugFreezeSupplyCurrent`) did NOT
remove it (a near-identical oscillation persisted), ruling out the sag loop as the cause. This circuit has no global
negative feedback loop to open in the first place (confirmed absent on the drawing), so that usual first diagnostic
step doesn't apply here at all -- the coupling instead runs through the **shared cathode-bias node**: all four EL84s
(both phases) are tied to ONE cathode resistor (confirmed on the drawing), which lets a current imbalance between
the two phases feed back into both of them through that shared impedance.

**What worked**: softening `kg1` on EITHER the LTP triode or the output EL84 pair ALONE reached genuine stability,
but needed a large enough multiple on either one alone to visibly flatten the reference model's own driven-signal
response into a near-dead-zone (output barely changing across two decades of input level) -- the same "fix crushes
the curve" failure the JCM800's own power-stage investigation found. Splitting a smaller multiple across BOTH tubes
(6x each) reached the same genuine 10 s stability with much less damage to either one's own transfer curve --
confirming the JCM800's own lesson generalizes to a second, unrelated circuit. See
`triode12AX7Pi()`/`pentodeEL84Pair()`'s own comments in the source for the exact reasoning.

**A separate finding, not a bug**: the reference model's preamp (V1a/V2a/V2b at Volume=0.8, the setting used for
calibration) saturates the cathode follower almost immediately -- its own output swings ~200-290 V peak even for the
smallest tested input level. This made the calibration sweep's own "small-signal" region hard to pin down cleanly
(the first couple of sweep points didn't yet reflect steady-state behaviour), so the fit below uses the well-behaved
middle portion of the sweep and treats the sweep's own highest levels (unrealistic input amplitudes, well beyond
anything a guitar produces) as out of scope, similar to how other amps on this roadmap have handled an irregular
sweep tail.

## Verification (`Tests/AC30StyleAmplifierProcessorTests.cpp`)
* DC operating point: plate rail 400.0 V (assumed, not printed -- see the table above), preamp rail 400.0 V
  (post-choke; idle current is too low to sag it noticeably before the 290 V decoupling), preamp plate 143.9 V,
  follower out 197.4 V, LTP plates at 332.7 V / 332.7 V (symmetric at idle, as expected), EL84 plates 398.8 V each,
  shared cathode bias 7.0 V, total plate current 107.2 mA (~53.6 mA per pair, plausible for four Class A EL84s).
* **Silence settles over a full 10 s soak**: peak 0.007177, zero failures, with the kg1 fix in place (see above).
* A plucked note through each Input (High/Low) setting stays finite with a low failure rate.
* `reducedOrder` tracks the (now-stable) reference within **-1.87 to +1.98 dB** across a 5e-3 to 0.6 V sweep, and
  4/8/16 ohm speaker settings are bit-identical.
* Worst-block cost under a hot-pedal stress: **4.63% avg, 7.2% worst block, zero failures/recoveries**.
* `PedalUnityLevelTests` passes at 0.00 dB with a measured registry trim of **-11.63 dB** (was +25.88 dB against the broken reference).

## Reduced-order (behavioural) power stage
Built in from the start, using the same `if (! reducedOrder)` guard pattern around the entire LTP/pentode/output-
transformer netlist that every amp since the Super Lead uses (`behavioralPowerStage()` is driven directly from the
preamp's own cathode-follower output when active, matching the AC15's own convention).
* **Fitted constants** (2026-10-05, against the corrected reference): `bmGain0 = 17.3`, `bmYmax = 0.0583`, `bmKneeN = 3.0`. Superseded: `bmGain0 = 0.00128`, `bmYmax = 0.00105` -- fitted from the well-behaved
  middle portion of an `A30_POWERCAL` sweep (see the instability section above for why the sweep's own extremes were
  excluded).
* **Verified**: level tracks the reference within -1.87 to +1.98 dB; worst-block cost and speaker 4/8/16 behaviour
  match the established pattern.
* **Known, documented gaps**: same as every amp on this pattern -- the speaker's own resonance isn't reproduced by
  the behavioural curve, and Bias/Tube Feel have reduced effect in `reducedOrder`.

## Cost
Shipped default (`reducedOrder`): ~4.63% avg, ~7.2% worst block under a hot-pedal stress, zero failures. Orders
{0, 0, 1}, same tier convention as the other amps. Unity trim +25.88 dB.
