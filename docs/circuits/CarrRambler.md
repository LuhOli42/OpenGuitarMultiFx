# Carr Rambler — physically modelled amplifier

The Carr Rambler is a ~28 W 1x12" cathode-biased combo: two 6L6GCs in push-pull with a rear-panel
pentode/triode switch, a long-tail-pair phase splitter, a blackface-derived single-channel preamp with a
TMB (Treble/Middle/Bass) tone stack, and bias-vary tremolo. It is best described as a cleaner, tighter
blackface-style platform — no global negative feedback.

## Source

Carr does not publish schematics. **Bryce Amps traced the preamp of a Rambler 2x12** (Dec 2011). It is a hand-drawn
page in their rare-schematic archive, `bryceamps.com/mainsite/carr rambler preamp section.pdf`, marked "for
information only". The preamp below follows it. The power stage was not traced, so it stays the documented estimate.

## What is modelled

- **Preamp** (`pre` NodalCircuit block), as traced:
  - **V1A:** 1M input leak (the stopper is unmarked; 68k kept), 100k plate, 1k5 + 22 µF.
  - **Tone stack:** a TMB fed **straight off V1A's plate**, with no coupling cap. It has 250 pF treble, 100k slope,
    100n bass / 47n mid legs, Treble 250k, Bass 250k and **Mid 25k** (the old model used 10k).
  - **Volume:** 1M from the Treble wiper, with a **47 pF bright** cap. Its wiper goes straight to V1B.
  - **V1B:** 100k plate, **1k8** + 22 µF.
  - **Into V3A:** 22n, then **2M4** into V3A's grid, which has 220k to ground. That is a ~1/12 divider. The reverb
    return joins there through 430k, modelled as its ~490k to AC ground. The reverb driver's 500p + 1M hangs on the
    22n.
  - **V3A:** 100k plate, 1k5 + 22 µF, then 22n into the PI.
  - The previous model stopped at V1B, with a coupling cap ahead of the stack. It had no 2M4 divider and no V3A
    stage.
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

## Reduced stage after the trace

The traced preamp makes the full netlist's PI / power stage block on hot input: its output *falls* from the quiet
chord to the loud one. A static saturator cannot do that. The reduced stage was refitted to the best compromise:

- level trim 0.205 → 0.30;
- knee 6 → 12;
- tail low-pass opened to 9 kHz, with air at 12 kHz.

The result is 1.35 dB of level and 5.06 dB of band shape. The shape bound moved from 5.0 to 5.5 for that reason. The
registry trim is −8.88 dB, giving unity.

## Power section: the 5E5 cathodyne (2026-10-31)
User report: the Rambler played as a high-gain amp although it sits under Clean. Owners describe the real amp as clean
to 2-3 o'clock on the Volume with single coils, with headroom at least equal to a Deluxe Reverb's (TDPRI, The Gear Page).

The power stage had been an estimate. An el34world member who drew a Rambler on loan reports: "the PA section is
exactly like the 5E5, not the 5E5a since it has no NFB and is cathode bias". He gives the OT as 6K6 / 8 Ω, the plates
at 366 V and the screens at 363 V. The Fender 5E5 Pro (el34world schematic) drives its 6L6s from a **cathodyne**
(56k plate, 1k5 + 56k cathode, 1M grid return to the junction). It has unity gain per phase, where the model's
long-tailed pair had ~20× and drove the 6L6s into saturation at half Volume.

The model now uses that cathodyne, the 5E5's .1 µF couplings and 250 Ω + 25 µF cathode bias, a 370 V plate rail and a
PI rail at 0.82 of the screens (+300 on +370 in the 5E5). The reduced path was refitted to it at Volume 0.2, 0.5 and
0.8. It had been fitted at 0.8 only, where everything saturates, and ran 9-11 dB hot at low Volume. The new constants
are gain0 6 and a +7 dB top shelf at 2.5 kHz (no NFB: the voice coil's inductance lifts the top).

| Check | Result |
|---|---|
| Reduced vs full | level 0.9 dB, band 4.7 dB (bound 1.5 / 5) |
| Volume 0.2 | linear to ~0.2 V in |
| Unity | −0.55 dB on the old trim |
