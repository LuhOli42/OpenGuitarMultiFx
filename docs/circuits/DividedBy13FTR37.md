# Divided by 13 FTR 37 — physically modelled amplifier

The Divided by 13 FTR 37 is a ~37 W cathode-biased combo famous for two genuinely different,
*interactive* channels sharing one output stage. The real amp runs **four** 6V6GTs in push-pull-parallel
(the model brief's "2x6V6" is the simplified spec often quoted; the model implements the real 4-tube
complement — the bias/current figures reflect that). Channel 1 is a single-ended-gain-flavoured "5879
pentode into Click position" voice; Channel 2 is a 12AX7 cascade with a Treble/Bass tone stack and a
pull-boost on the Volume. Channel interaction: both feed the same mix node and fight for the power stage.

## What is modelled

- **Channel 1** (`preCh1` NodalCircuit block): a 5879 pentode (EF86-class Koren fit, screen strapped at
  ~100 V as measured on FTR boards) into a 12AX7 triode stage, tapped at the "Click" gain position into
  the shared mix node. The pentode lives in its own solve — a Koren pentode sharing a Newton block with
  triodes diverged during bring-up.
- **Channel 2** (`pre` block): 12AX7 gain stage -> 0.022 µF -> Volume (track halves) -> blackface-style
  Treble/Bass stack (250 pF / 100k slope / 0.1 µF + 0.047 µF legs, Bass rheostat bridging the two cap-end
  nodes, fixed 6k8 tail where a Mid pot would sit — same wiring as the Deluxe model's proven stack) ->
  recovery triode -> 0.022 µF into the shared mix node. The Volume knob's pull position switches in a
  cathode-bypass cap (the real Mid+Gain boost).
- **Mix/interaction**: both channel taps feed `pMix` through 100k resistors — the same "jumpered channels"
  pattern the Fender-family models use, so each channel loads the other when both volumes are up.
- **Power stage** (`power` block, theta = 0.9): LTP phase inverter (12AX7, 82k/100k plates, 470 + 22k
  tail), 4x 6V6GT in push-pull-parallel, **fixed** bias with a front-panel bias trim, Full/Half power
  switch (halves the 6V6 idle current the way the real cathode/bias change does), OT + speaker impedance
  network, three speaker voicings.
- **Supply** (`supply` block, theta = 0.5): rectifier -> reservoir -> screen tap, drawn-current feedback,
  decoupling taps to PI and preamp rails.

## Deliberate simplifications

- The Click control is modelled as a tap-level attenuator at the documented tap point rather than the
  real amp's full switch-ladder of cathode/grid changes — the continuous knob maps onto the same
  function (gain staging before the mix).
- No NFB (matches the real amp), no tremolo/reverb (the FTR 37 has neither).
- `reducedOrder` swaps the nodal power block for the shared behavioural sag/power model.
- One-channel-at-a-time operation is the intended use; both channels are always solved (cheap) so
  interaction is real, but the model does not attempt the real amp's input-jack switching tricks.

## Solver notes

- `preCh1` and `pre` are separate solves exchanging the Ch1 tap per-sample as a source — the same
  boundary pattern used between preamp and power blocks in the existing amps.
- Output DC removed by a ~5 Hz one-pole tracker (the real coupling into the LTP).
