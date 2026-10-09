# AC15-Style Amplifier (Vox AC15, 1959/1960 "Twin" chassis, Channel I)

Display name **"AC15-Style Amplifier"** (category Modeled Amps), registry key `AC15StyleAmplifier`. Runs on
[`NodalCircuit`](./NodalCircuitSolver.md). Code: `Source/Effects/AC15StyleAmplifierProcessor.{h,cpp}`, tests
`Tests/AC15StyleAmplifierProcessorTests.cpp`.

**Genuinely new circuit family on this roadmap.** Every amp built so far (Bassman, Super Lead, Twin/Deluxe Reverb,
JTM45, JCM800) shares the same basic shape: triode preamp(s), a long-tailed-pair phase inverter, fixed-bias push-pull
output tubes, and a global negative-feedback loop. The AC15 breaks from that pattern on every point: an **EF86
pentode** preamp gain stage (not a triode), a **cathodyne (split-load) phase inverter** -- a single triode half with
equal plate/cathode load resistors producing two anti-phase outputs, not a differential pair -- a pair of **EL84s in
Class A, cathode-biased** (self-bias, sharing one resistor, no fixed bias supply at all), and **no global feedback
loop whatsoever**.

## Source
`schematicheaven.net/voxamps/ac151959.pdf` -- The Jennings Organ Co.'s own "AC/15 AMPLIFIER No.2 (CIRCUIT)", dated
4-12-59. Fetched via `curl`, read from the rasterized scan (`pdfimages` extraction, upscaled crops for legibility).
This is the UK "Twin" chassis: two channels sharing one power section -- **Channel I** (an EF86 pentode preamp) and
**Channel II** (an ECF82 triode-pentode feeding a "Vibravox" tremolo unit). A later American-tube-numbered "V-1-5"
schematic (`ac15.pdf`, same site) turned out to be a DIFFERENT, later two-ECC83-channel AC15 variant with no EF86 at
all -- checked and set aside once the topology mismatch was confirmed, in favour of the 1959 original.

## Scope: Channel I only
Channel II's own ECF82 preamp and its "Vibravox" tremolo oscillator are **not modelled**, matching this project's
standing rule against modelling a built-in vibrato/tremolo oscillator (the same call already made for the Twin
Reverb/Deluxe Reverb's own vibrato channels). Only Channel I -- the EF86 preamp, famous in its own right as this
amp's defining sound -- feeds the shared phase inverter and power section modelled here.

## What is modelled
```
guitar -> High or Low sensitivity jack (a plain input attenuation, High=1.0/Low=0.25, both sharing one EF86 grid)
       -> 220k grid stopper, 1M grid leak -> EF86 (1.5k cathode + 25 uF bypass, 100k plate, ~200 V screen)
       -> 0.02 uF -> Tone (a single treble-cut control, no Bass/Middle/Presence) -> Volume (1M log)
   [power section, shared with Channel II on the real amp -- Channel II itself not modelled]
       -> cathodyne (split-load) phase inverter: ONE 12AX7 half, equal 47k plate/cathode loads (confirmed on the
          drawing) -> two anti-phase outputs
       -> each output: 0.05 uF into its own EL84's 10k grid stopper (confirmed, symmetric both sides)
       -> two EL84s (a real push-pull pair, own grid/plate each) sharing ONE 130 ohm cathode-bias resistor with a
          bypass cap (confirmed 130 ohm; the bypass value wasn't legible, 50 uF assumed) -- CATHODE-BIASED, NO fixed
          bias supply
       -> each plate through its own 100 ohm series resistor (confirmed) -> output transformer (no negative
          feedback at all) -> 16 ohm tap -> speaker
   [supply] EZ81 rectifier -> 8H/120mA choke (both confirmed) -> 450 V B+ (confirmed) -> a decoupled preamp tap
```
Controls (0..1), **page 1**: Input (High / Low sensitivity jack), Volume, Tone, Output (a plug-in level control; the
real amp has no master volume). **Page 2**: Power Drive (a synthetic master ahead of the phase inverter, matching
this project's convention for every amp on this roadmap, real or not), Bias (a synthetic shift of the shared
cathode-bias resistor -- the real amp has no adjustable bias trim, being self-biased), Tube Feel, Speaker (4/8/16
ohm; the real amp ships a fixed ~15 ohm speaker, rounded to this project's 16 ohm tap).

## Reading choices and assumptions -- change these first if the amp sounds wrong
| What | Value | Why |
|---|---|---|
| EF86 screen voltage | fixed ~200 V | the real amp's own screen-dropping network wasn't legible on the scan; 200 V is a plausible, datasheet-safe value (EF86's own rating tops out around 300 V) rather than the raw ~380 V decoupled preamp tap, which would badly overdrive the tube |
| EL84 screen voltage | fixed 290 V | same reasoning -- EL84's own rating also tops out around 300 V, well under the 450 V main B+; a dedicated screen supply/dropping network is assumed, not derived from a literal resistor |
| EF86/EL84 Koren tube fits | published SPICE parameter sets (EF86: mu 34.9, Kg1 2648.1, Kp 222.06, Kvb 4.7, Ex 1.35; EL84: mu 16, Kg1 570, Kg2 4200, Kp 50, Kvb 24, Ex 1.35) | not independently re-fitted to this specific amp's own measured curve the way the Marshall/Fender power tubes on this roadmap were (this circuit's own reference model turned out to be well-behaved and stable with the published sets, so no re-fit was needed -- see "Verification" below) |
| Output transformer | 8k plate-to-plate, 20 H plate-to-plate inductance, 16 ohm tap | not printed; a much smaller core than the 100 W Marshalls on this roadmap, so a leaner bass corner is expected and not a bug |
| Coupling caps, grid leaks not fully legible on the 1959 scan | reasonable standard values (0.02 uF couplings, 1M/220k grid leaks) | the scan's resolution made some component labels illegible at the pixel level; values chosen are standard for this class of circuit, not schematic-confirmed to the exact microfarad |
| Bias (page 2) | a synthetic 0.7x-1.3x scaling of the shared 130 ohm cathode resistor | the real 1959 amp has no adjustable bias trim at all (self-biased) -- this knob exists only for UI consistency with every other amp on this roadmap's page-2 layout |

## Verification (`Tests/AC15StyleAmplifierProcessorTests.cpp`)
* DC operating point: plate rail 450.0 V (matches the drawing's own printed value), preamp rail ~450 V (post-choke,
  the decoupled preamp tap sits close to it at idle since idle current is low), EF86 plate 129.7 V, cathodyne PI
  plate/cathode split 444.5 V / 5.5 V (a real, if fairly cold, 12AX7 bias point for this exact 47k/47k topology --
  matches the equal-load-resistor self-biasing behaviour a cathodyne is expected to show), EL84 plates ~447.8 V each,
  shared cathode bias 15.0 V, total plate current 115.6 mA (~57.8 mA per tube, a plausible Class A EL84 push-pull
  idle current).
* **Silence settles over a full 10 s soak** (the JTM45/JCM800 lesson applies to every new topology, not just amps
  that showed trouble) with zero drift/failures on the very first attempt -- this circuit did NOT show the
  tube-loop self-oscillation failure mode those two amps did, despite being an entirely new topology.
* A plucked note through each Input (High/Low) setting stays finite with a low failure rate.
* `reducedOrder` tracks the reference within **0.14-0.45 dB** across a 5e-3 to 0.6 V sweep, and 4/8/16 ohm speaker
  settings are bit-identical.
* `PedalUnityLevelTests` passes at -0.00 dB with a measured registry trim of **+37.37 dB** -- unusually large, but
  not a sign of a broken model: the AC15 is a genuine 15 W amp (not the 100 W Marshalls this roadmap started with),
  its EF86/cathodyne/EL84 preamp-to-power chain has real, if modest, gain at each stage, and the calibration sweep's
  own curve is a clean, monotonic saturating shape (no irregularity of the kind that made the JCM800's own reference
  unreliable to calibrate from) -- a large trim safely restores correct loudness without distorting the dynamics.

## A real bug found and fixed while building this amp: `reducedOrder` provided no CPU savings at all
The first working draft built the full cathodyne-PI + 2xEL84 + output-transformer netlist UNCONDITIONALLY in
`buildChannel()`, with `reducedOrder` only switching which value `debugVoltage(Probe::speaker)`/`process()` reported
(`ch.bmOutput` vs the real `ch.power.voltage(ch.wOut)`) -- unlike every other amp on this roadmap, which skips
building the expensive PI/pentode/transformer netlist entirely in `reducedOrder` mode. This meant `reducedOrder` was
doing ZERO Newton-solve work reduction (the whole point of the pattern), and it also caused a genuine, reproducible
test bug: because `reducedOrder` is a STATIC flag shared by every instance, one test's `red` object (built with
`reducedOrder = true`) left the flag `true` for the REST of the same test's sweep loop, so the `ref` object's own
"reference" measurement was silently ALSO going through the (placeholder, uncalibrated) behavioural curve instead of
the real circuit -- explaining an initial, wildly wrong-looking 58 V reference reading that vanished once both bugs
were fixed together (the real reference tops out under 1 V for this amp, a big and important difference from the
100 W Marshalls). Fixed by wrapping the entire power-section build in `if (! reducedOrder)` (matching the Super
Lead/JCM800 pattern) and driving `behavioralPowerStage()` directly from the preamp's own Volume-pot output rather
than an `ch.power` node that no longer exists in that mode; the test now explicitly resets the static flag before
each side of its own comparison loop. **Lesson for the next genuinely-new-topology amp**: verify `reducedOrder`
actually measurably reduces `Tests/EffectCostBench.cpp`'s own per-sample cost, not just that the numbers it reports
look plausible -- a probe that merely re-labels the same computation can pass every accuracy test while doing none
of the job reducedOrder exists for.

## Reduced-order (behavioural) power stage
Built in from the start, calibrated (`A15_POWERCAL`) against this amp's own real reference model -- no instability
investigation was needed here (see the section above for what almost went unnoticed instead).
* **Fitted constants** (2026-10-05): `bmGain0 = 3.22`, `bmYmax = 0.1017`, `bmKneeN = 1.5`. The earlier `0.070` /
  `0.002837` ("far smaller than every other amp because it is a 15 W design") were NOT a property of the amp: the
  reference had each EL84 plate shunted to B+ through its 100 ohm resistor (wired rail-to-plate instead of in series
  with the primary), killing ~35 dB of output, and the behavioural stage was fed the Volume node with its ~150 V DC
  still on it. Both fixed; the reference now reaches ~21-26 V rms into 16 ohm. The preamp tap also got an RC
  decoupling filter (22k + 16 uF assumed), see docs/circuits/AC30TopBoost.md. Registry trim +37.37 dB -> +4.30 dB.
* **Verified**: level tracks the reference within 0.14-0.45 dB across the tested sweep; speaker 4/8/16 behaviour
  bit-identical; worst-block cost under a hot-pedal stress: **2.58% avg, 9.5% worst block, zero failures/recoveries**.
* **Known, documented gaps**: same as every amp on this pattern -- Presence/Bias/Tube Feel have reduced or no effect
  in `reducedOrder` (there is no Presence knob on this amp regardless), and the speaker's own resonance isn't
  reproduced by the behavioural curve.

## Cost
Shipped default (`reducedOrder`): ~2.58% avg, ~9.5% worst block under a hot-pedal stress, zero failures -- the
cheapest amp on this roadmap so far (the smallest circuit: no PI/pentode/OT Newton solve at all in this mode).
Orders {0, 0, 1}, same tier convention as the other amps. Unity trim +37.37 dB.
