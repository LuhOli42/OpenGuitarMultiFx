# Bass Big Muff-Style Fuzz (EHX Bass Big Muff Pi)

The Big Muff circuit for bass: same four-NPN-cell topology as `BigMuffStyleFuzz.md` (input stage ->
Sustain divider -> two feedback-diode clipping cells -> passive treble/bass-blend tone stack -> output
recovery stage), voiced in the Sovtek/Russian family, plus the two bass-specific controls.

## Sources

- Kit Rae's Big Muff history (kitrae.net): the Bass Big Muff (EC-D40 era) sits in the Sovtek/Russian
  sound family -- "heavier, fatter", the tall-font/Civil-War descendant, not the USA V3 voicing.
- The same sources' description of the two toggles: **BASS BOOST** lets more low end into the circuit,
  **DRY** turns the Volume knob into a wet/dry blend where the dry level is constant and the knob adds
  distortion on top.

## What is modelled

- The full four-stage netlist at the Russian-family values (identical to this repo's
  `MuffModel::russianGreen` spec: 12k/390R cells, 500 pF feedback caps, 0.047 uF clip-branch caps,
  R8 20k tone split) -- the lower-gain, bass-keeping voice.
- **BASS BOOST** as the input-coupling capacitor switch: C1 goes from 0.022 uF (input HPF corner ~170 Hz,
  tilting a bass signal down) to 0.22 uF (~17 Hz, flat) -- more of the bass register reaches the clip
  stages, which is exactly what the real toggle does ("lets more low end into the circuit").
- **DRY** as a 33k resistor summing the input source node into the output node downstream of the Volume
  divider: constant-level dry + whatever the Volume wiper passes of the distortion, matching the real
  pedal's "Volume becomes a blend" description.

## Deliberate simplifications

- The real bass-boost network's exact switched component placement (it sits across two positions in
  some traces) is folded into the single input-cap switch -- same audible mechanism (bass into the
  clip), one handled capacitor.
- Transistor betas assumed at 500 (2N5089-class), same as the Russian model.
- Controls: Sustain, Tone, Volume + Bass Boost and Dry toggles (stepped params so the unity suite
  leaves them at their defaults).
