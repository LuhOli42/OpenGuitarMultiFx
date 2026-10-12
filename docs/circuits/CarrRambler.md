# Carr Rambler — physically modelled amplifier

The Carr Rambler is a ~28 W 1x12" cathode-biased combo: two 6L6GCs in push-pull with a rear-panel
pentode/triode switch, a long-tail-pair phase splitter, a blackface-derived single-channel preamp with a
TMB (Treble/Middle/Bass) tone stack, and bias-vary tremolo. It is best described as a cleaner, tighter
blackface-style platform — no global negative feedback.

## What is modelled

- **Preamp** (`pre` NodalCircuit block, two cascaded 12AX7 stages in the real blackface signal order —
  corrected after an audit found the earlier draft was wired as a Trainwreck cascade, which made the
  Rambler distort at every volume):
  - V1A input stage: 68k grid stopper, 1M leak, 100k plate load, 1k5 cathode + 22 µF bypass.
  - 22 nF coupling into a blackface TMB: 250 pF treble bypass, 100k slope, 0.1 µF bass / 0.047 µF mid
    legs, 250k-A treble pot, 250k-A bass rheostat, 10k-A mid rheostat to ground.
  - Single 1M-A Volume pot (track halves modelled as two resistors) tapped at the treble wiper;
    the wiper feeds V1B's grid directly — the pot track is the grid leak, as in the real wiring.
  - V1B recovery stage: wiper-fed via 68k, 1k5 + 22 µF cathode, 100k plate load.
  - 22 nF coupling into a 1M "PI grid leak" — the preamp's output tap.
  - Miller grid-to-plate capacitance on each triode.
- **Power stage** (`power` block, theta = 0.9): LTP phase inverter (5751-family triode fit), 2x 6L6GC
  (KorenPentode defaults), cathode-biased, cathode-vary tremolo injected on the bias node, output
  transformer + speaker impedance model with the standard `rSpkRe/rSpkRp/rSpkEddy` network and the three
  speaker-voicing positions.
- **Pentode/triode mode switch**: pentode mode ties screens to the screen rail; triode mode follows the
  plate taps per-sample, exactly the real switch's screen-to-plate strap.
- **Supply** (`supply` block, theta = 0.5): rectifier resistance -> reservoir -> screen node, current-draw
  feedback from measured pentode currents (200 ms one-pole, the same damping pattern other amps use),
  decoupling-tap model to the PI and preamp rails.

## Deliberate simplifications

- The two triodes of the real V1 are modelled as two stages (input -> stack/volume -> recovery), the
  blackface family the Rambler descends from, not a traced schematic (Carr does not publish one).
- No global NFB — matching the real amp.
- Tremolo is amplitude/bias-vary only (no optocoupler phase subtleties); LFO is a sine.
- `reducedOrder` replaces the nodal power block with the shared behavioural sag/power table
  (`sagRailLookup`) for CPU-bound targets; the preamp stays physical.
- Output DC is removed by a ~5 Hz one-pole tracker on the preamp tap — the modelled equivalent of the
  real coupling cap into the PI.

## Solver note

The three-stage preamp is kept in a single `NodalCircuit` solve (the proven Trainwreck shape). An early
split-block variant and several topology variants exhibited slow basin-hopping wander at idle; the
single shared solve is stable. Test-side, `process()` is in-place — buffers must be re-cleared every
block in tests, or the amp eats its own output and any model looks like it self-oscillates.
