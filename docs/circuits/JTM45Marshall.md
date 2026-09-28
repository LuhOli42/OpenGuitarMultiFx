# JTM45-Style Amplifier (Marshall JTM45)

Display name **"JTM45-Style Amplifier"**. Runs on [`NodalCircuit`](./NodalCircuitSolver.md), the same three-part
structure (preamp, tone-stack/power block, supply model) as the Bassman -- unsurprisingly, since the JTM45 is
historically Marshall's first amp and a near-direct copy of the Fender 5F6-A Bassman's own circuit. This processor
is built heavily on `BassmanStyleAmplifierProcessor`'s own code, with the real, schematic-confirmed differences
applied on top.

## Source
Schematicheaven.net's "JTM45 first" period document ("BASIC SCHEMATIC FOR MARSHALL TREM AMPS, TYPES 1961, 1962,
1987/T"), fetched from `schematicheaven.net/marshallamps/jtm45_lead_45w.pdf`. Read directly from the rasterized
drawing at 300 dpi. (A separate file on the same site, confusingly named `jtm45_first.pdf`, turns out to be scans of
the Fender Bassman's OWN schematic and layout sheets -- likely included there because the earliest JTM45 boards were
literally hand-traced from that exact document -- and was not used as a source here beyond confirming that history.)

## What is modelled, and what differs from the Bassman
1. **Preamp**: the same 12AY7 shared-cathode dual-triode input stage + 12AX7 gain stage + cathode follower as the
   Bassman, same 270k mixing resistors. The schematic reads the two channels' OWN plate load resistors as asymmetric
   (180k for one triode half, 100k for the other) where the Bassman uses a symmetric 100k/100k -- a real, confirmed
   difference. **Not currently used, see "A power-stage instability" below**: isolation testing found the asymmetric
   values measurably worsen this amp's own power-stage instability, so symmetric 100k/100k values are used instead,
   a disclosed fidelity trade-off in favour of stability.
2. **Tone stack**: the same well-documented 250k treble / 1M bass / 25k middle pot values the Bassman uses (Marshall
   reused these), but the treble bypass cap reads 270 pF on this amp's own schematic, not the Bassman's 250 pF -- a
   real, confirmed, and used difference.
3. **Phase inverter**: identical 12AX7 long-tailed pair to the Bassman's own.
4. **Power tubes**: KT66, not the Bassman's 5881 -- see "A power-stage instability" below for why this circuit did
   not end up with an independently-fitted KT66 curve the way the Bassman's 5881 was.
5. **Rails**: this amp's own schematic prints 310 V / 380 V / 460 V at the preamp / PI / output-tube rails (close to,
   but not identical to, the Bassman's own printed 325 / 385 / 452 V). **Not currently used**: the Bassman's own rail
   values are used instead -- see below.
6. **Bias**: not separately printed on this amp's own voltage chart; assumed -48 V (the Bassman's own printed value),
   a plausible fixed-bias point for a KT66 pair at a similar rail, not independently verified.

## A power-stage instability that was not fully resolved

Unlike every other amp on this project's roadmap, this circuit's full-topology reference model has a genuine,
persistent low-frequency self-oscillation that an extensive investigation did not fully explain or eliminate within
the time available. This section exists so a future pass does not have to re-discover the same dead ends.

**What was ruled out, one at a time, via clean isolation tests:**
- **The global negative feedback loop.** Opening it entirely (`debugSetFeedbackResistance(1e9)`) did not stop the
  oscillation, and neither did more than doubling the feedback resistor (54k -> 120k, weakening it substantially
  without fully opening it). The instability lives upstream of, or independent of, this loop.
- **Preamp plate-load symmetry.** Asymmetric (180k/100k, the schematic's own values) vs. symmetric (100k/100k, the
  Bassman's) plate loads: the asymmetric version was measurably WORSE (0.82 peak vs. 0.15, otherwise identical
  settings) -- the opposite of "no effect," so this one is a real, if modest, contributing factor. Symmetric values
  are used as a result.
- **The power-tube grid-bias network's shared-node topology.** The Bassman's own "g3/g4 -> shared node -> decoupling
  cap -> bias rail" shape was replaced with direct per-grid resistors to the ideal bias source (the exact fix that
  resolved an analogous, and at-the-time seemingly identical-looking, Deluxe Reverb instability -- see
  `circuit-tube-model-loop-instability` in project memory). It made no measurable difference here, ruling out that
  specific mechanism for this circuit even though it looked like the same failure signature.
- **This amp's own rails vs. the Bassman's.** A clean A/B (default KT66 pentode fit, symmetric preamp, everything
  else held constant) found this amp's own printed rails (460/380/310 V) measurably LESS stable than the Bassman's
  own (452/385/325 V) -- for reasons not understood (the two rail sets differ by only 1-2%, not enough to obviously
  explain the difference in outcome). The Bassman's own rails are used as a result, a disclosed deviation from this
  amp's own printed values.

**What does move the needle: the KT66 pentode's own `kg1` (transconductance).** Raising it far past Koren's published
default softens the tube's own gain enough to eventually reach genuine stability -- but this took MUCH more testing
discipline than expected, because several intermediate values looked stable in a short (1-2 s) silence test and then
grew into a full-scale, sustained oscillation over several more seconds once measured over a full 10 s soak. (The
project's own silence-settle test for this amp was widened from 2 s to 10 s specifically because of this finding --
a short window is not sufficient evidence of stability for this particular circuit.) The value that IS confirmed
stable over a full 10 s soak, `kg1 = 300000` (roughly 200x Koren's published 1460), comes at a real cost: the power
stage's own small-signal gain and saturation behaviour are far weaker than a real KT66's, needing a much larger drive
signal to reach the same relative saturation the Bassman's 5881 reaches easily, and reducing how much the power
stage's own character (as opposed to the preamp's) can be heard. The registry trim (`+21.60 dB`, unusually large for
this pattern) compensates for the resulting overall quietness, but does not restore the missing dynamic character.

**What was tried and made things WORSE, confirming this isn't simply "not enough gain reduction":**
- Reusing the Bassman's own independently-fitted 5881 curve (`mu = 15.96, kg1 = 889.4, kp = 23.2`) on this amp's
  power section, at either rail set: substantially worse than even the unfitted Koren default.
- Combining the Bassman's fitted `mu`/`kp` shape with a moderate `kg1` bump (5000): also worse than the plain
  default-mu approach.
- A very damped integrator (`powerTheta = 0.98` vs. the usual 0.9) with the default pentode: much worse (a
  full-scale runaway, not a bounded oscillation).

**Working hypothesis, not confirmed**: the true root cause is likely something structural neither inherited unchanged
from the Bassman nor independently re-derived for this amp -- the output transformer's turns ratio/primary
inductance (assumed, not printed, identical to the Bassman's own placeholder values) is the most likely remaining
suspect, since it sets the loop's overall gain scale in a way none of the isolation tests above directly probed. A
future pass with more time should re-derive this from a real JTM45 OT's published specs rather than reusing the
Bassman's, and re-run the same isolation sequence with a properly-scaled OT before returning to tube-parameter
tuning.

## Verification (`Tests/JTM45StyleAmplifierProcessorTests.cpp`)
DC: plate rail 452.0 V (near the printed 460 V, within tolerance), PI rail 385.0 V, preamp rail 325.0 V -- all against
the Bassman's own targets per the disclosed rail substitution above. Silence settles over a full 10 s soak (not the
usual 2 s -- see above) with zero drift/failures. A plucked note through each Input setting stays finite and bounded.
`PedalUnityLevelTests` passes at 0.00 dB with a measured registry trim of **+21.60 dB**.

## Reduced-order (behavioural) power stage
Built in from the start, calibrated only after the instability above was reduced to a genuinely stable (if weak)
reference model -- calibrating against an oscillating reference would have produced garbage. Same mechanism as the
other amps on this pattern; see the Super Lead's own doc for the full methodology.
* **Calibration** (`JM_POWERCAL`): drive point is the tone stack's own output (`toneStackOut`), same as the Bassman.
  Because of the softened pentode fit, this amp needs a MUCH larger drive swing to reach saturation than the Bassman
  does -- the output plateaus around 0.55-0.56 V once the rail has sagged into the 440-441 V range (vs. the rail's
  452 V idle), a real but modest ~2.7% sag.
* **Fitted constants**: `bmGain0 = 0.225`, `bmYmax = 0.001278` -- both far smaller than the Bassman's own numbers
  (8.23 / 0.092), a direct, expected consequence of the softened pentode needing far more drive for the same relative
  output.
* **Verified**: level tracks the reference within **0.01-0.8 dB** across a 5e-3 to 0.6 V sweep (the 0.8 dB is only at
  the very smallest tested level, 0.01 dB and under everywhere else) -- as tight as this project's best amps on this
  pattern, for the usual reason (an empty `ch.power` in reduced mode). Worst-block cost under the same hot-pedal
  stress the other amps were measured against: **5.72% avg, 8.4% worst block, zero failures/recoveries by
  construction**.
* **Speaker (4/8/16 ohm)**: bit-identical across all three settings, same `speakerGain = 1.0` fix as the other amps.
* **Known, documented gaps**: everything the other amps on this pattern already document (Presence/Bias/Tube Feel
  have reduced or no effect in reducedOrder), PLUS the power-stage character weakness described above, which is
  specific to this amp and the largest fidelity gap on the roadmap so far.

## Cost
Shipped default (reducedOrder): ~5.72% average, ~8.4% worst block, zero failures. Orders {0, 0, 1}, same tier
convention as the other amps. Unity trim +21.60 dB (unusually large -- see "A power-stage instability" above for why).
