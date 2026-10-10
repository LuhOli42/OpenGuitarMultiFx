# Carr Rambler — physically modelled amplifier

The Carr Rambler is a ~28 W 1x12" cathode-biased combo: two 6L6GCs in push-pull with a rear-panel
pentode/triode switch, a long-tail-pair phase splitter, a blackface-derived single-channel preamp with a
TMB (Treble/Middle/Bass) tone stack, and bias-vary tremolo. It is best described as a cleaner, tighter
blackface-style platform — no global negative feedback.

## What is modelled

- **Preamp** (`pre` NodalCircuit block, three cascaded 12AX7 stages sharing one Newton solve — the
  TrainwreckExpress structure, which is the proven-stable topology in this solver):
  - V1A input stage: 68k grid stopper, 1M leak, 100k plate load, 1k5 cathode + 22 µF bypass.
  - 22 nF coupling into the single 1M-A Volume pot (track halves modelled as two resistors).
  - V1B recovery stage: wiper-fed via 68k, 2k7 + 0.68 µF partial-bypass cathode.
  - 22 nF coupling into a cold unbypassed 10k stage (the stack driver).
  - Blackface TMB hanging directly on the stack driver's plate (rides at plate DC): 500 pF treble cap,
    100k slope, 0.1 µF + 0.047 µF legs, 250k treble/bass rheostat wiring, fixed tail, treble wiper =
    stack output, 1M "PI leak" to ground.
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

- The two triodes of the real V1 are modelled as two *stages*, but the real amp's exact tube count on the
  recovery path is folded into the three-stage cascade; component values follow the blackface family the
  Rambler descends from, not a traced schematic (Carr does not publish one).
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
