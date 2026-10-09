# Komet Concorde-Style Amplifier

Processor: `Source/Effects/KometConcordeStyleAmplifierProcessor.{h,cpp}` (registry key `KometConcordeStyleAmplifier`), built on
the Trainwreck Express-style processor (`docs/circuits/TrainwreckExpress.md` -- read that first; this file only records what is
different and how well each difference is documented).

## Sources

- Komet Amplification, Concorde product page: <https://kometamps.com/products/amplifiers/concorde> -- 50 W, EL34s, solid-state
  rectifier, single channel, Volume, 3-band EQ, Presence, Hi-Cut; a higher-gain, more aggressive voicing than the Komet 60.
- Komet Concorde owner's manual, ManualsLib:
  <https://www.manualslib.com/manual/935507/Komet-Concorde.html> -- front panel Volume, Bass, Middle, Treble, Presence, Hi-Cut;
  Hi-Cut shapes the treble in the power amp without altering the preamp EQ; Presence affects the highest frequencies in the
  output stage.
- Komet 60 product page <https://kometamps.com/products/amplifiers/komet-60> (the related Komet design the Concorde is
  compared against).
- Amp Garage, K60 / Concorde build thread: <https://ampgarage.com/forum/viewtopic.php?start=15&t=30635> -- a builder
  listing the Concorde's differences from the K60: diode rectified, same power transformer, **4k8 output
  transformer**, remove the 220k after the .1 uF on the fast/gradual switch, and **use V2's spare triode as a cathode follower**
  (V2A plate direct to V2B grid, 100k cathode resistor to ground, the follower feeding the .022 uF / 220k at the PI's input).
- Fractal Audio forum, "Comet Concourse" (Komet Concorde) model thread:
  <https://forum.fractalaudio.com/threads/fractal-audio-amp-models-comet-concourse-komet-concorde.112872/> -- no reverb, master,
  loop or footswitch; all-tube signal path.

There is **no complete public Concorde schematic**. Documented = Komet's own statements + the builder's list above. Everything
else is carried over from the Express / K60 family and is an estimate.

## What the Concorde actually is (relevant to the model)

- Trainwreck/Komet-family preamp: 12AX7 stages ending in a **V2B cathode follower** -> .022 uF / 220k -> PI (documented by
  the builder). The order of the earlier stages, the stack and Volume is **estimated** (gain -> stack -> Volume -> recovery ->
  cold gain stage -> follower).
- LTP phase inverter (inferred from the Trainwreck lineage), 2x EL34 (documented), **4k8** primary (builder report). Bias
  scheme not published; fixed bias like the Express is assumed.
- **Solid-state rectifier** (documented), so the B+ sits higher than the GZ34 Komet 60.
- Hi-Cut in the power amp, Presence in the output stage, no global NFB loop is described anywhere (inference from the
  Trainwreck lineage and the manual's description of Presence as an output-stage control).

## What is modelled (differences from the Express)

- Preamp: V1 (100k / 1k5 + 22 uF) -> the anode-driven stack (Express values, order estimated) -> 1M audio Volume -> recovery stage (100k / 2k7 +
  0.68 uF) -> .022 -> cold stage (100k / 10k unbypassed) -> **V2B follower** (12AX7, plate on the preamp rail, 100k cathode
  to ground, direct-coupled grid).
- Power: .022 uF / 220k from the follower, then the same LTP / EL34 / OT structure as the Express, with a **4800 ohm** primary and a
  **Hi-Cut** network (variable R 2k-1M + 1 nF across the PI plates).
- Supply: 460 V plate rail (estimated), 100 uF reservoir (estimated), PI ~335 V and preamp ~300 V (estimated).
- Bias noon -51 V, span +/- 7 V (estimated: ~40 mA idle on the fitted EL34 set at these rails).

## Tubes

Same file-local fits as the Express (Koren 12AX7; the Super Lead EL34 set, single tube).

## Controls

| Control | Real amp | Model |
|---|---|---|
| Volume, Treble, Middle, Bass | documented on the panel | as the Express (stack values estimated from the Express) |
| Presence | documented, output stage | estimated network: variable R + 10 nF across the primary |
| Hi-Cut | documented, power amp | estimated network: variable R + 1 nF across the PI outputs |
| Touch (Fast / Gradual) | the fast/gradual switch named in the builder thread | **estimated**: Gradual = the first grid driven at 0.35x (~-9 dB) |
| Page 2 | -- | Power Drive / Bias / Tube Feel / Speaker / Output as the Express |

## Honest simplifications

- All component values not in the builder's list are Express values or estimates; this is "a Trainwreck-family amp with the
  documented Concorde differences", not a trace of a Concorde chassis.
- The Touch switch is a level approximation, not the real cap/resistor network (not public).
- The follower is a full Koren triode (not an ideal follower) because it is direct-coupled from the cold stage and its grid
  current / clipping are part of the Concorde's documented character.
- Same supply / OT simplifications as the Express.

## Stability

Same as the Express: no global NFB loop, bypassed bias node, AC-grounded second PI grid, `prepare()` after the full netlist.
Zero input for 1.5 s after warm-up must give AC RMS < 0.01 (unit test) and `AmpAudibleOutputTests` gates it.

## Verification targets

- DC: plate rail 430-490 V, PI rail 290-360 V, follower within a few volts below the cold stage's plate (grid-cathode bias), EL34 grids -38..-58 V, idle
  15-65 mA per tube.
- Hi-Cut up removes 5 kHz; Gradual is quieter than Fast; Volume / Treble / Bass / Presence / Bias as the Express.
- RenderFx over both DI fixtures with sane full-band spectra.
