# Darkglass B7K-Style Overdrive (Darkglass Microtubes B7K)

Modern bass overdrive/preamp: buffered input -> Grunt/Attack pre-clip voicing switches -> TL072-class
drive stage -> 4049 CMOS-inverter clipping (the "Microtubes" engine) -> clean/wet Blend -> 4-band EQ
(Low / Lo-Mid / Hi-Mid / Treble) -> Level.

## Sources

- Darkglass B7K user manual: front panel is Blend, Level, Drive, Bass(100 Hz), Lo-Mid(1 kHz),
  Hi-Mid(3 kHz), Treble(5 kHz) + Grunt and Attack 3-way switches; input Z 500k.
- Community clone traces (e.g. the "Black Mirror" B7K-Ultra BOMs): J201/TL07x buffers and gain stages,
  a 4049N CMOS inverter pair used as biased inverting amplifiers for the clip, anti-parallel 1N4148s
  shunting the clipper output, and a gyrator-based 4-band Baxandall-style EQ.

## What is modelled (block A..D netlists)

- **A**: input coupling + follower, then three pre-clip shaping networks: Grunt (a switched series-RC
  HF-bleed -> relative bass boost), Attack (3-way: series-R+shunt-C treble cut / flat / bright-cap
  treble shelf), and the drive op-amp (saturating TL072 macro, gain ~6x..100x on the Drive pot).
- **B**: the CMOS clipper -- an inverting stage on the 4049-style macro (10k in / 220k fb, rails
  0.4..8.6 V = near rail-to-rail CMOS swing) plus the anti-parallel silicon diode shunt to the bias.
- **C**: Blend (each leg through half a 47k track into the summing node) + 4-band EQ built on the
  GE7 gyrator-band technique: Low = gyrator-inductor branch shelving ~100 Hz, Lo-Mid and Hi-Mid =
  series-C + gyrator resonators at ~970 Hz and ~3.3 kHz, Treble = series-RC shelf ~5 kHz.
- **D**: output coupling + Level divider.

## Deliberate simplifications

- The real EQ uses dedicated gyrator op-amps per band inside the feedback of one summing amp; the
  model uses the project's folded-inductor equivalent of the same network (same impedance, fewer nodes).
- Band sweep range is ~ +/-15 dB (the GE7-style network's natural depth) vs the quoted +/-12 dB.
- The exact Grunt/Attack RC values are not published; they are voiced to the documented direction
  (bass-shelf amount / treble cut-flat-boost) rather than traced values.

## Pipeline engine: compiled, not applied (2026-10-18)
`pipeline::CompiledCircuit` compiles all four blocks (the EQ gyrators as inductors, the EQ block with two inputs). Against
the netlist: level 0.06 dB, small-signal band 0.1 dB, x1.7 faster; on hot bursts the netlist throws one-sample spikes at
the clipper (2.3 V on a silicon pair to the bias, sample 30318 of the burst test; the compiled engine goes -0.50 then
-0.67 V there), so peak / envelope cannot be compared. It is NOT the e^40 cap on NodalCircuit's diode exponential:
replacing it with SPICE-style linear continuation left the spike identical (11.26 dB). Most likely the netlist's
step-limited (pnjlim) Newton accepting an unconverged point at the reversal. `pipelineEngine` stays false until that
is explained.

### Correction (2026-10-18, later)
The hot-burst peak mismatch above was mostly the compiler's, not the netlist's: Gauss-Seidel solved the clipper's diode
port before the drive op-amp's rail limit, and that limit (a 1-ohm source when pinned) carries large currents, so the
diode worked from a stale value. With `railsFirst` the peaks match (0.27 dB) and band is 0.30 dB; the 5 ms envelope is
2.8 dB off at the recovery after the drive stage, above the 1.5 dB bar, so it stays off. The netlist's one-sample
excursion seen at sample 30318 is real in the netlist output but is not what the peak metric was measuring.
