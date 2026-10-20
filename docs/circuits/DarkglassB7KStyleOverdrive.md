# Darkglass B7K-Style Overdrive (Darkglass Microtubes B7K)

Modern bass overdrive/preamp: buffered input -> Grunt/Attack pre-clip voicing switches -> TL072-class
drive stage -> 4049 CMOS-inverter clipping (the "Microtubes" engine) -> clean/wet Blend -> 4-band EQ
(Low / Lo-Mid / Hi-Mid / Treble) -> Level.

## Sources

- Darkglass B7K user manual / product page (darkglass.com, archived 2015): front panel is Blend, Level, Drive,
  Bass +-12 dB @ 100 Hz, Lo Mids +-12 dB @ 1 kHz, Hi Mids +-12 dB @ 2.8 kHz, Treble +-12 dB @ 5 kHz, plus Grunt and
  Attack 3-way switches; input Z 500k. (The later B7K **Ultra** adds Lo Mid 250/500/1k and Hi Mid 750/1.5k/3k
  switches; that is a different pedal and is not what this block models.)
- Community clone traces (e.g. the "Black Mirror" B7K-Ultra BOMs): J201/TL07x buffers and gain stages,
  a 4049N CMOS inverter pair used as biased inverting amplifiers for the clip, anti-parallel 1N4148s
  shunting the clipper output, and a gyrator-based 4-band Baxandall-style EQ.

## What is modelled (block A..D netlists)

- **A**: input coupling + follower, then an estimated 2.2 kΩ series feed resistor into the pre-clip shaping
  networks (the dry path still taps the buffer output). Grunt is an estimated 0.15 µF + switched-resistor
  series-RC shunt at the drive input, bleeding highs for relative bass boost; the strongest-bleed 4.7 kΩ
  position has a corner near 226 Hz. Attack (3-way: series-R + shunt-C treble cut / flat / bright-cap
  treble shelf) follows the feed resistor, then the drive op-amp (saturating TL072 macro, gain ~6x..100x
  on the Drive pot).
- **B**: the CMOS clipper -- an inverting stage on the 4049-style macro (10k in / 220k fb, rails
  0.4..8.6 V = near rail-to-rail CMOS swing) plus the anti-parallel silicon diode shunt to the bias.
- **C**: Blend (each leg through half a 47k track into the summing node) + 4-band EQ built on the
  GE7 gyrator-band technique: Low = gyrator-inductor branch shelving ~100 Hz, Lo-Mid and Hi-Mid =
  series-C + gyrator resonators at 963 Hz and 2.8 kHz (measured boost peaks, below), Treble = series-RC shelf ~5 kHz.

### EQ centres (measured 2026-10-19, OD/dist audit; probe sweep, Drive 0, Blend dry, one band at max vs all flat)
| Band | Published | Before | After |
|---|---|---|---|
| Bass | 100 Hz | +13.6 dB peak at 84 Hz | unchanged |
| Lo Mid | 1 kHz | +11.3 dB at 963 Hz | unchanged |
| Hi Mid | 2.8 kHz | +11.2 dB at **3.30 kHz** | +11.1 dB at 2.86 kHz (gyrator C 2.2 nF -> 3.0 nF) |
| Treble | 5 kHz | +6.1 dB at 5 kHz, +8.8 dB at 10.7 kHz (shelf) | unchanged |

The Hi Mid resonator sat 0.24 octave above the published centre (and the source comment claimed ~1.2 kHz, which the
measurement shows was never the case); its gyrator capacitor is retuned so 1/(2 pi sqrt(Cs Cgyr Rgyr Rloss)) = 2.8 kHz.
- **D**: output coupling + Level divider.

## Deliberate simplifications

- The real EQ uses dedicated gyrator op-amps per band inside the feedback of one summing amp; the
  model uses the project's folded-inductor equivalent of the same network (same impedance, fewer nodes).
- Band sweep range is ~ +/-15 dB (the GE7-style network's natural depth) vs the quoted +/-12 dB.
- The Grunt/Attack RC values and the 2.2 kΩ feed resistor are estimates, not published/traced values; they
  are voiced to the documented direction (bass-shelf amount / treble cut-flat-boost).

## Pipeline engine: applied (2026-10-19)
`pipeline::CompiledCircuit` compiles all four blocks (the EQ gyrators as inductors, the EQ block with two inputs) and is
the default engine since PR #132: level 0.00 dB, band 0.07 dB, peak 0.00 dB, 5 ms envelope 0.24 dB against the netlist,
11.6 -> 5.0 % of a core. `setPipelineEngine (false)` switches back to the netlist.

How it got there (docs/PerformancePipeline.md, "Three fixes"):
- `railsFirst`: Gauss-Seidel solved the clipper's diode port before the drive op-amp's rail limit and worked from a
  stale value (the 11 dB peak gap first seen);
- the compiler now integrates with the netlist's theta (0.6) and clamps the op-amp macro's integrator node like the
  netlist's diodes (the 2.8 dB envelope at the recovery after the drive stage);
- the netlist's one-sample spike at the clipper (2.3 V on a silicon pair, sample 30318 of the burst test) WAS the
  step-limited Newton accepting an unconverged point, as first suspected here: NodalCircuit now requires such a point
  to balance to 50 mV. It was not the e^40 cap on the diode exponential (replacing it changed nothing).
