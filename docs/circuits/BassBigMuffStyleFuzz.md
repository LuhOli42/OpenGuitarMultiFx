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
  tilting a bass signal down) to 0.082 uF (~50 Hz with 39K, flat across the bass register) -- more of the bass
  reaches the clip stages, which is exactly what the real toggle does ("lets more low end into the circuit").
- **DRY** as 10K from the input source to the output node, alongside 22K from the Volume wiper to that
  node: constant-level dry + whatever the Volume wiper passes of the distortion, matching the real
  pedal's "Volume becomes a blend" description.

## Deliberate simplifications

- The EC-D40 Rev.A traces on freestompboxes (t=31296; dylan159, 2021 / G. Bisson, 2022) show the real
  Bass Boost as a low-passed path (R30 / C31, roughly 300–400 Hz) mixed into the tone-stack output, not
  an input-capacitor switch. They also show many capacitors doubled in parallel (for example,
  100 nF || 100 nF = 200 nF coupling caps). Neither detail is modelled: the traced component values are
  incomplete (C38 is unknown), so the implementation keeps the simpler input-cap switch and Russian-family
  coupling values rather than inventing missing parts.
- Transistor betas assumed at 500 (2N5089-class), same as the Russian model.
- Controls: Sustain, Tone, Volume + Bass Boost and Dry toggles (stepped params so the unity suite
  leaves them at their defaults).

## Cost, oversampling, reduced order
One `NodalCircuit` block: four two-port BJTs plus two anti-parallel diode pairs.

* **reducedOrder (shipped, 2026-10-11):** the four transistors become one-port saturating devices
  (`NodalCircuit::addBjtSaturating`), 4 Newton ports dropped. Same saturation load lines fitted for the
  Russian family (x1.5 input, x2.0 clip, x1.5 output of (rail - Vce(sat)) / (Rc + Re) -- identical resistor
  values, so the multipliers carry over). `ReducedOrderEquivalenceTests`: worst level 0.32 dB / worst
  octave band 0.45 dB over sustain x tone x bass-boost x input.
* Aliasing (non-harmonic/harmonic): **-26.2 dB at 1x, -46.5 at 2x, -69.3 at 4x** -- the Russian row in
  `BigMuffStyleFuzz.md` measured the same, so tiers **1x / 2x / 4x**.
* Eco-tier cost (dev box, 48k/256/mono): **21.9% -> 12.8% of a core** with the reduced netlist
  (audit `tools/fx-audit/cpu-bench-2026-10-11-r2.md`: 26.8% -> ~16-19% projected on the A7S).
* Unity trim **-8.40 dB** (the Russian voice plus the low stage gains is quiet at noon).

## Pipeline engine (default since 2026-10-17)
Compiled with `pipeline::CompiledCircuit` (docs/PerformancePipeline.md): 13.8 % -> 3.8 % of a core; against the
netlist level 0.44 dB, band 0.44 dB, peak 0.30 dB, envelope 1.32 dB. The transistor exponential port is left off here:
it made the hot-burst tails worse. `setPipelineEngine (false)` = the netlist.
