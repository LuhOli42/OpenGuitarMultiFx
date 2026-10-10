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
