# Ampeg B-15 Portaflex (B-15N, rev C)

## What it is

The small flip-top combo that defined 1960s-70s bass tone: ~25 W through three 6SL7 octal dual
triodes and a pair of 6L6GCs, with a 5AR4 rectifier. Front panel: Volume, Bass, Treble (the later
B-15Ns add Ultra-Lo and Ultra-Hi rocker switches on the input voicing). Inputs: two channels
(High/Low sensitivity). Fixed-bias push-pull 6L6s, ~-50 V grid supply.

## What's modelled

- The real front end: a 6SL7 gain stage (470k plate / 5.6k cathode, bypassed) into the Ultra-Lo
  scoop divider, the SVT-family passive tone net (bass pot 1M audio taper, treble 250k, series
  resistor and treble cap scaled for 6SL7 impedance), a second 6SL7 recovery stage (220k / 2.2k
  bypassed), the Volume and Master pots.
- The floating-paraphase phase inverter the B-15N actually uses: V3a is both the last gain stage
  and the first phase-splitter half; a fraction of its plate signal is tapped off (470k/10.4k
  divider) into V3b, which amplifies it into the second output phase. Both share a feedback node
  where global NFB from the speaker winding is summed.
- The power section: fixed-bias 6L6GC pair (Koren model), ~-50 V bias string, 220 pF grid-stopper
  caps, the output transformer (coupled inductors, ~8k plate-to-plate, 4/8/16 ohm taps), the
  speaker impedance model, and 100k + 0.1 uF feedback to the paraphase summing node.
- The supply: 5AR4-typical ~395 V plate rail sagging through a 260 ohm rectifier resistance and a
  6.8k dropper to the ~330 V preamp rail, both driven by measured pentode plate+screen current.

## Honest simplifications

- The two input channels are identical apart from sensitivity pads — covered by the 0 / -15 dB
  Input switch.
- 6SL7 Koren parameters are estimated (mu 70, ex 1.4, kg1 1200, kp 400) — no published fit exists;
  operating points were checked against the schematic's expected ~180-200 V plates.
- OT primary (~8k p-p) and the feedback network values are documented estimates; rev-C schematics
  are image scans without legible text for every secondary tap.
- A Master control is kept (synthetic, like the other amp models) because the B-15's only level
  control is the channel Volume.

## Reduced-order power stage (shipped default)

Like the JCM800/SLO-100/Mark IIC+/Dual Rectifier/5150/Powerball/Rockerverb/Deluxe Reverb/AC30/Bassman/
Super Lead family (see CircuitFamilies.md), this processor ships a behavioural power stage as the
application default (`AmpegB15StyleAmplifierProcessor::reducedOrder`, set centrally in EffectRegistry.cpp
together with `markQualityDependent`): the preamp and tone-shaping netlists remain real circuits,
while the phase-inverter + push-pull + output transformer + global NFB + speaker impedance cluster is
replaced by an envelope-followed sag rail feeding an asymmetric tanh knee, a DC blocker, and a
low-shelf speaker-magnetics correction, fitted to this file's own reference netlist. The full netlist
stays in the source for calibration and is what the unit tests exercise (`reducedOrder = false` at the
top of the test).
