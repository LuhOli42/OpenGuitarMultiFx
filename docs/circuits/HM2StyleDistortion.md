# HM-2-Style Distortion

Display name **"HM-2-Style Distortion"**. Runs on
[`NodalCircuit`](./NodalCircuitSolver.md).

## Sources
Two: the factory schematic (hobby-hour.com; dense, low resolution) and a clean
redraw (experimentalistsanonymous.com) that has legible values and is what the
netlist was transcribed from, with the factory drawing used to settle the one
ambiguous thing (the DIST pot, below). The redraw omits the output transistors
(Q3/Q10) and the LED/switch; the pedal's output is taken at the Level wiper.

## Topology
1. **Q1 (2SK30A-Y) JFET source follower**: R 10K, C 47 nF, 1 M gate bias to
   4.5 V, 10K source.
2. **Q6 (2SC2240, NPN)**: input C 47 nF + R 22K; base bias only through
   **470K || 10 pF from its own collector** plus 100K to ground; 22 ohm emitter,
   10K collector load — self-biased, very high gain.
3. **Q7 (2SA970, PNP)**: same idea, 100K + 470K || 10 pF bias, 120 ohm emitter
   degeneration, 10K collector load to ground and 68K to 4.5 V. Both stages run
   from a 1K + 47 uF filtered 9 V.
4. **Clipping op-amp IC1B**: (+) is Q7's collector; feedback is **1 diode one way
   and 2 in series the other** (asymmetric, like the OD-1) in parallel with 220K
   and 10 pF; the (-) leg is 47K + 47 nF from the DIST branch.
5. **Germanium pair in series with the signal** (10K, GE diodes, 10K), then a
   **silicon pair to ground** and 1 nF, then C 1 uF into IC1A (unity buffer).
6. **Color**: IC1A -> 3.3K -> IC3A (differential op-amp, 3.3K feedback); the
   **LOW and HIGH pots' tracks run from IC3A's (+) node to its (-) node**; their
   wipers are fed by three unity-gain followers, each with a capacitor from its
   (+) node to the node after its 330 ohm output resistor — a **gyrator**
   (simulated inductor, L = C x R x 330 ohm): IC3B 68 nF/100K (2.2 H) with 1.5 uF
   for the bass, IC2B 4.7 nF/100K and IC2A 6.8 nF/82K with 0.1/0.15 uF for the
   mids/highs. Then C 10 uF, 10K, Level.

   Pot tapers: VR4 DIST 250K "D" (audio/log) and VR1 Level 10K audio use the shared 15% law
   (`Source/Effects/PotTaper.h`); LOW/HIGH are 10K linear.

### The DIST pot (decoded literally)
Boss draws VR4 250KD with its **wiper grounded** and its two ends in the signal
line (150 ohm on one side, 47K on the other). Electrically that makes it two
*shunts*: segment A (pin 1 -> wiper) loads Q6's collector, segment B (wiper -> pin
3) is the op-amp's gain leg. Turning it up reduces A's loading and shrinks B, so
signal and gain rise together. Modelled exactly as drawn.

## Block layout (and the bug this replaced)
Three blocks: **(1)** input JFET, Q6, Q7, the DIST branch **and the clipping
op-amp IC1B** (with its diodes and 220K/10 pF feedback); **(2)** the germanium /
silicon network after the op-amp, driven by IC1B's output; **(3)** the Color
section and Level.

IC1B must live in block 1. The op-amp's (-) node is where the gain stage, the DIST
branch and the feedback network all meet, and an earlier version cut the netlist
*there*, predicting that node from the two previous samples. That looked harmless
(the error is small in the branch current) but the feedback resistor multiplies it:
a 47 nF capacitor at fs = 48 kHz is ~3.8 mS, times 220K of feedback is a **~840x
amplification of the prediction error**, which showed up as glitches and a hash of
noise on everything the pedal played -- "the gain stage isn't working properly".
With the op-amp in the same block as the (-) node the coupling is exact:
the energy above 12 kHz at IC1B's output dropped from -28 dB to -56 dB (relative to
the fundamental), and the steady-state output now repeats to -154 dB
(`OversampledEffectTests`, "steady-state clean"). The lesson is general and is in
`NodalCircuitSolver.md`: never cut a block on a node whose downstream gain is high.

The two series diodes of the "2 in series the other way" branch are one
`addDiode` with twice the thermal voltage (identical diodes in series with nothing
on the middle node are exactly that), which drops a node and a Newton port.

## Cost simplification, second pass (2026-09-21, revisited 2026-10-11) -- shipped with the bc-junction diode (see last section)
Q6 and Q7 are one-port transistors with a smooth collector-current limit (`NodalCircuit::addBjtSaturating`,
`HM2StyleDistortionProcessor::reducedOrder`; `false` = the two-port Ebers-Moll netlist, kept as the reference): the collector
junction, which needs a second Newton port, only matters as the point where a stage runs out of collector current, and
for a stage with a resistive load that is known. Limit = the load line, fitted (Q6 x1.9, Q7 x0.9) so that a decaying chord
at 3 Dist x 2 High x 3 input levels keeps its level within ~1.2 dB and its octave-band shape (bands above -30 dB) within
~1.1 dB. That is the limit of this idea: a macro-model like the BD-2's does not exist here because the two stages are
self-biased common-emitter stages that saturate as part of the sound, not op-amps with a feedback loop. Block 1 went from
5 ports to 3 (6.6% -> 5.4% of a core). The user heard a volume "pop" at high gain
right after it went in. It could not be reproduced with chords, a real DI, gated input, note on/off, knob moves and silence (envelopes
within 0.5 dB), but the two variants differ on hard edges: windowed peaks up to ~8-10 dB apart on a 2 Hz square wave and on a
gated sine. The fix is in the last section: the bc junction restored as a diode on Q6 (`satBcDiode6`), and `reducedOrder`
now ships.

2026-10-11 investigation (pop mechanism): the one-port device never draws the saturation base
current the two-port Ebers-Moll does -- in saturation the reverse junction's current exits through the base,
iB += (iF - iC)/(betaR + 1) -- so a saturating stage under-drew base current and recovered differently on edges. A gated
version of that current in `NodalCircuit` (gate r^16/(1+r^16), ~0 at DC bias, ~1 in hard saturation) did halve the edge
difference (~8 -> ~4 dB) -- but it also moved the chord-equivalence corner past the bar (dist 0.1, hot input: worst band
1.4-2.1 dB vs the 1.2 dB limit, vs 1.07 dB without it), and no combination of Q6/Q7 limits satisfied both at once
(q6 x1.6: band 1.68 / edge 3.8; q6 x1.9: band 1.37 / edge 7.9). The fix was reverted; the Big Muff's shipped reducedOrder
does not need it. If this is picked up again, the remaining gap is the feedback-bias corner, not the base current.
## Cost simplification (2026-09-20)
Q1 (the input JFET source follower) is an ideal follower with its DC level shift (-0.96 V);
timbre within 0.13 dB of the full model. Q6/Q7 (the self-biased stages that clip), the
op-amp's diode network and the Ge/Si pairs remain devices; antiparallel pairs are single
ports.

## Device parameters (service-manual parts list + Toshiba's datasheets)
Parts list (hobby-hour's transcription of the Boss service docs): Q1/Q4/Q5/Q10 =
**2SK30A-Y**, Q2/Q3 = 2SC732TM-GR, Q6 = **2SC2240-GR**, Q7 = **2SA970-GR**,
Q8/Q9 = 2SC945-P; D1/D3/D5/D8-D12 = 1S2473 (Si switching), D6/D7 = 1S188FM (Ge).

- **2SK30A-Y**: Idss Y rank 1.2-3.0 mA -> **2.1 mA** midpoint, Vgs(off) ~ -1.0 V
  (was 4 mA / -1.5 V, a GR-grade guess -- the -Y grade is what the parts list
  gives, and it sits Q1's source follower ~0.5 V above the 4.5 V bias, not ~1 V).
- **2SC2240-GR / 2SA970-GR**: hFE GR 200-400 -> **beta 300** (was 400, the top
  edge). Both stages are self-biased via 470K collector feedback, so DC barely
  moves inside the rank.
- Silicon clipper diodes = 1S2473 -> 1N4148-class (Is 2.52 nA, N 1.752);
  "GE.DIOD" = 1S188FM -> 1N34A-class as in the Centaur model.
- The 4.5 V bias rail is treated as ideal.

## Verification (Tests/HM2StyleDistortionProcessorTests.cpp)
- DC: Q1 source ~5.0 V (the Y-grade 2SK30A at this bias: gate 4.5 V, source a
  Vgs of ~-0.5 V above it), Q6 collector 4.12 V (~0.4 mA),
  Q7 Veb 0.63 V, Q7 collector 3.83 V.
- Q6+Q7 gain at 1 kHz: 160x (44 dB).
- Color: Low up/down = 87x at 90 Hz and High up/down = 136x at 1.2 kHz (small
  signal); Low at 1.2 kHz vs flat = 1.03x — each control acts in its own band.
- Solver: 0 failures per block across every Dist setting under a hot input.
- Cost ~3.3 us/sample (16% of a core, mono) at the host rate; the registry runs it
  2x oversampled, see [Oversampling](./Oversampling.md).

## Not modelled
Output transistors, LED, bypass; op-amp rail clipping (the M5216/M5616 on 9 V);
the 4.5 V divider's source impedance.

## reducedOrder SHIPPED (2026-10-11): the missing port was the bc junction itself, not a drain coefficient
The #79 pop mechanism was re-tested twice before this:

1. `iB += iC_unsat - iC_sat` in the kind-4 saturating one-port -- overshot badly (worst band 9.31 dB on
   HM-2, 2.86 dB on the Big Muff; reverted).
2. A harness sweep (HM2_SWEEP in Tests/HM2StyleDistortionProcessorTests.cpp) of a proportional drain
   coefficient `iB += k*(iC_unsat - iC_sat)`, k in 0.001..0.3, in both injection directions (base->emitter
   and base->collector). No k satisfies the bounds: the pop floor sits ~2.4-3 dB and every nonzero k
   pushes the chord band error past 1.2 dB by k ~= 0.015. Any drain term active during sustained clip
   moves the DC bias corner -- the feedback-bias corner is what breaks, not the transient.

What actually works: restore the junction the one-port lacks. The two-port Ebers-Moll's bc junction clamps
vbc and conducts the saturation current; the one-port has no port for it at all. Adding it back as a real
diode -- `addDiode(b, c, Is/betaR * satBcDiode6, nVt)` (mirrored for the PNP) -- only conducts while the
stage is in saturation, so it is invisible to the DC/band fingerprint yet provides exactly the drain path
the self-bias network needs on edges. Measured on the sweep harness (hot square 0.5 amp / dist 0.9 /
high 0.8, the documented worst case): windowed-peak edge error 6.11 -> 0.30 dB, gated-sine error
3.31 -> ~1.1 dB, chord equivalence level 1.14 / worst band 1.04 dB -- flat across satBcDiode = 0.05..2.0
(a diode is on once its Is matters at all; only the knee moves). Per-transistor isolation rows in the same
sweep show Q6's junction is the entire pop: satBcDiode6 = 0.25 alone scores identically to a diode on both
stages, while satBcDiode7 alone is indistinguishable from no diode -- so the shipped model restores Q6's
junction only (satBcDiode6 = 0.25, satBcDiode7 = 0), one Newton port cheaper. `reducedOrder` is enabled
in EffectRegistry.

Residual: a smaller ~2.4 dB windowed-peak difference remains on the mild gated-0.5-dist control and is
identical at every coefficient including 0 -- a separate reduced-model difference, not the edge pop.
CPU (RenderFx --bench, 48k mono 256-block): the wrapped eco path stays ~27% of a core either way -- the
2x oversampling and wrappers dominate, so enabling reducedOrder never was a <10% route; the remaining
lever is Orders{0,...} at eco, a measured-aliasing tradeoff documented in cpu-bench-2026-10-11.md.
