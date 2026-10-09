# Trainwreck Express-Style Amplifier

Processor: `Source/Effects/TrainwreckExpressStyleAmplifierProcessor.{h,cpp}` (registry key `TrainwreckExpressStyleAmplifier`).
The Komet Concorde (`docs/circuits/KometConcorde.md`) is built from the same class and shares this research.

## Sources

- Trainwreck Express schematic set, Amp Garage archive: <https://ampgarage.com/forum/files/wreckxpr.pdf> -- builders' traces
  of real amps, several variants that differ in details (100k plates, 1k5 / 2k7 cathodes, 22 uF bypass, 250k vs 1M tone pots,
  ~5k plate-to-plate OT, 8 / 16 ohm taps, B+ nodes of about 395 / 380 / 295 / 282 / 267 V).
- *The Trainwreck Pages* (Ken Fischer's own notes, hosted and annotated by Rob Robinette):
  <https://robrobinette.com/The_Trainwreck_Pages.htm>. Fischer: the Liverpool 30 "employs a cathode biasing system which
  requires no adjustment" with four EL84s; the Express takes two EL34s (or 6V6s) and is biased "by the Voltage Method by
  setting the grid voltage ... -30 volts on Pin 5 of the output tube, which may be adjusted by using the variable adjuster
  inside the chassis".
- Builders' field readings quoted in the same threads (individual builds, not a spec): first-stage plates ~175-203 V,
  later preamp plates ~257 V, PI plates ~223-232 V, EL34 plates ~369-389 V.

Trainwreck never published service data; everything here is either Fischer's notes or builders' traces, and they disagree
in details.

## What the Express actually is (relevant to the model)

- **Preamp: three cascaded 12AX7 triode stages, one Volume knob.** V1B (100k plate, 1k5 cathode, 22 uF bypass) -> .022 ->
  the 1M audio **Volume** -> V1A (100k plate, 2k7 cathode) -> .022 -> V2A (100k plate, ~10k cathode, unbypassed: a "cold"
  clipping stage; ~257 V on its plate in the field readings is consistent with ~0.3 mA through 100k from a ~285 V node). There is no master volume, no
  gain knob separate from Volume, no channel switching.
- **Anode-driven tone stack.** The treble / bass / middle network hangs straight off V2A's plate -- no cathode follower --
  with a 500 pF treble cap, ~100k slope resistor and .022 bass / mid caps (Marshall TMB topology, Trainwreck values). Some
  revisions show 250k pots, others 1M bass.
- **Long-tailed-pair phase inverter** (third 12AX7): 82k / 100k plates, a small bias resistor over a ~22k tail (values vary by
  variant; estimated in the model).
- **Two EL34s**, .022 couplings, 220k grid leaks into a **fixed (negative) bias supply** with a bias trimmer -- Fischer's
  "-30 volts on Pin 5", adjustable inside the chassis.
- **~5k plate-to-plate output transformer**, 8 / 16 ohm taps.
- **No global negative feedback.** Nothing returns from the secondary to the PI. The "Presence" knob therefore cannot be the
  usual NFB-shelf; the drawings show a small cap/pot network in the output stage (see "Controls"). Robinette and builders
  stress that a no-NFB EL34 stage is harsh without a conjunctive (Zobel-style) R-C across the primary/secondary.
- Solid-state rectified supply, first filter in the 80 uF region, a 1k dropper to the screens and kilohm-class droppers down to
  the PI / preamp nodes (395 / 380 / 295 / 282 / 267 V on the drawings).

### About "cathode-biased"

The session brief described the power pair as cathode-biased. In the Trainwreck lineage the cathode-biased amp is the
**Liverpool 30 (4x EL84)**; Fischer's own notes say the Express is set "by setting the grid voltage ... -30 volts on Pin 5"
with an internal adjuster, i.e. **fixed bias**, and the drawings show the negative supply and trimmer. The model follows the
documented Express (fixed bias, adjustable on page 2). A cathode-biased variant would be a small change in
`buildChannel()` (shared cathode R + bypass in place of the bias node).

## What is modelled

Three NodalCircuit blocks per channel, same structure as the AC15 / Super Lead models:

1. **preamp** (`pre`): input 68k stopper + 1M leak, V1B, Volume (1M audio, `pots::audio`), V1A (2k7 with an estimated
   0.68 uF partial bypass), V2A (10k unbypassed), the anode-driven stack (500 pF / 100k / .022 / .022, 250k audio treble,
   250k lin bass, 25k lin middle) loaded by the PI's 1M grid leak.
2. **power** (`power`): 0.1 uF into the LTP (1M leaks to the tail tap, the other grid AC-grounded through 0.1 uF), the
   Presence network, .022 couplings, 220k leaks into the bias node (15k from the bias source, 10 uF), 1k5 grid stoppers, two
   individual EL34 pentodes (own grid / plate each, a real push-pull pair), coupled-inductor output transformer (centre tap on
   the plate rail, 16 ohm secondary), winding capacitance / loss network, and the project's speaker model.
3. **supply** (`supply`): diode + winding resistance, reservoir, 1k to the screen node; the PI and preamp rails follow the
   screen node through first-order RC lags (the dropper / filter chain). Plate and screen currents are fed back every 8 samples.

## Tubes

- 12AX7: Koren's ECC83 set (`KorenTriode::Parameters{}`), as everywhere in this project.
- EL34: the Super Lead's fitted EL34 set (mu 8.11, Ex 1.50, Kg1 1201, Kg2 3720, Kp 100, Kvb 24) with the pair-doubling
  removed, file-local in the .cpp. That fit's cutoff sits lower than a real EL34's (the Super Lead doc: a plexi is -55 V on
  it), so Fischer's -30 V would idle the model at >100 mA per tube. The model's bias noon is therefore -41 V, which gives
  ~40 mA (~15-16 W, ~70 % of an EL34's 25 W) -- matching the operating point, not the grid voltage.

## Controls

| Control | Real amp | Model |
|---|---|---|
| Volume | 1M audio pot between V1B and V1A | same (documented) |
| Treble / Middle / Bass | anode-driven TMB stack | same topology; values from the drawings, pot sizes per revision vary |
| Presence | small cap/pot network in the output stage (no NFB loop exists) | **estimated**: variable R (0.3k-60k) + 10 nF across the OT primary: knob down shunts the top octave off the primary (a conjunctive-filter-style cut), knob up removes it |
| Power Drive (page 2) | -- | synthetic master ahead of the PI (project convention; 1 = the real amp) |
| Bias (page 2) | internal bias adjuster | -47 .. -35 V grid, noon = -41 V on the fitted EL34 set, ~40 mA idle (Fischer sets a real Express to -30 V; see "Tubes") |
| Tube Feel (page 2) | -- | supply series resistance, 1 = the real amp |
| Speaker (page 2) | 8 / 16 ohm taps | 4 / 8 / 16 ohm speaker model on the 16 ohm tap (project convention) |
| Output (page 2) | -- | plug-in level |

## Honest simplifications (documented, not hidden)

- **Component values are a composite** of the drawing variants; where they disagree the field readings' operating point won. Estimated (not legible / not consistent on the drawings): V1A's partial bypass (0.68 uF), the exact
  slope resistor (100k), pot sizes, the Presence network values, the OT inductance (6 H per half), the 40 uF lumped screen /
  downstream filter, the 6 mA preamp + PI current.
- The bright cap(s) some revisions show on the Volume pot are not modelled.
- Screen voltages are a solved supply node, not a per-tube resistor network (both screens share the 1k dropper's node).
- The PI / preamp rails are first-order lags of the screen node rather than solved RC sections (their currents are tiny).
- Output transformer: linear coupled inductors, no core saturation / hysteresis.

## Stability

- There is no global feedback loop, so the SVT-style "wrong secondary sense = positive feedback" failure cannot occur; the
  secondary's winding sense only sets absolute output polarity.
- The bias node is a 10 uF-bypassed node behind 15k (the grid-bias bypass requirement), the PI's second grid is AC-grounded
  through 0.1 uF, and every block calls `NodalCircuit::prepare()` after the netlist and initial guesses are complete.
- Unit test: 1.5 s of zero input after a 1.5 s warm-up must give AC RMS < 0.01 with zero solver failures;
  `AmpAudibleOutputTests` gates the registered model on the same check.

## Verification targets

- DC: plate rail 370-420 V, screens 350-400 V, PI rail 260-320 V; first-stage plate 140-230 V / cathode 0.8-2.2 V (estimated
  from 1k5 at ~1 mA); second-stage cathode 1-3 V; cold-stage plate 220-285 V / cathode 1.5-4.5 V; PI plates 150-260 V; EL34 grids -30..-50 V (fitted-tube scale, see "Tubes"); idle 15-65 mA per tube, matched.
- Volume / Treble / Bass / Presence each move the output in the expected direction; Bias moves idle current.
- RenderFx over `Tests/fixtures/di/guitar-di.wav` and `bass-di.wav`: output spectrum must keep fundamentals, mids and top
  bands (no rumble-only or silent output).
