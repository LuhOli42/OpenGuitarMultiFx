# Ampeg V4B (1971)

## What it is

The 100 W all-tube bass head in Ampeg's V-series: the same circuit family the SVT-CL descended from.
Tubes: 4x 7027A output (a rated-up 6L6GC), 1x 12DW7 phase splitter (an asymmetric dual triode), 2x
12AX7A, 1x 12AU7A driver and 1x 6K11 compactron (a triple triode) doing the front end. Front panel per
channel: Volume; shared: Bass, Middle, Mid-Freq select (300 / 800 / 3000 Hz), Treble, Ultra-Lo and
Ultra-Hi rocker switches. Factory spec: 100 W into 2 / 4 / 8 ohm, input sensitivity ~8 mV.

## What's modelled

- The full signal path the SVT-family schematic actually has: two 12AX7 gain stages around the Ultra-Lo
  scoop network and the Volume pot; the passive Baxandall-style bass/treble section with the Ultra-Hi
  bright cap; the recovery stage; the tapped-inductor mid trap on a 3-position select (retuned to the
  V-series' 300 / 800 / 3000 Hz resonances); the Master pot and the cathode-follower output.
- The power amp exactly as drawn: a 12AX7 long-tailed-pair splitter (standing in for the 12DW7), two
  12AU7 common-cathode drivers with resistive level-shifters returning to a negative rail — the output
  grids' ~-50 V bias is the dividers' DC operating point — four 7027As as two push-pull pairs on fixed
  bias, the output transformer (coupled inductors, ~4k plate-to-plate into 4/8 ohm taps), the speaker
  impedance model, and global negative feedback into the second PI grid.
- The supply: plates ~530 V, screens/driver ~340 V, preamp ~300 V, each sagging under the measured load
  currents.

## Honest simplifications

- The 6K11 compactron is modelled with the 12AX7 parameter set (its triodes are in the same family); the
  12DW7's asymmetric halves are likewise approximated with the stock 12AX7 model.
- Only one channel is modelled — the two input channels are identical apart from input-pad voicing,
  which the 0 dB / -15 dB Input switch covers.
- OT primary impedance (~4k), the NFB resistor (150k) and the level-shifter's negative rail (-180 V)
  are documented estimates matching the Ampeg topology, not literal part numbers from the scan.
- A Master control is kept (synthetic, like the other amp models) because the V4B's only level control
  is the channel Volume.

## Power-stage stability

The raw Koren 6L6GC knee on the 7027A pair plus the unmodified LTP triode gave the global-NFB loop
enough incremental gain to sustain a ~3 Hz motorboating limit cycle on silence. As documented for the
JCM800/SLO-100/etc. in CircuitFamilies.md, the remedy is the same documented compromise: `kg1` is
softened ~40x on BOTH the phase-inverter triode AND the output pentode pair. The side effect is also
the documented one -- the reference model's idle current and closed-loop gain collapse far below the
real amp's ~160 mA quiescent (the unit test's idle-current bound reflects this), which is part of why
the behavioural power stage below is the shipped model.

## Reduced-order power stage (shipped default)

Like the JCM800/SLO-100/Mark IIC+/Dual Rectifier/5150/Powerball/Rockerverb/Deluxe Reverb/AC30/Bassman/
Super Lead family (see CircuitFamilies.md), this processor ships a behavioural power stage as the
application default (`AmpegV4BStyleAmplifierProcessor::reducedOrder`, set centrally in EffectRegistry.cpp
together with `markQualityDependent`): the preamp and tone-shaping netlists remain real circuits,
while the phase-inverter + push-pull + output transformer + global NFB + speaker impedance cluster is
replaced by an envelope-followed sag rail feeding an asymmetric tanh knee, a DC blocker, and a
low-shelf speaker-magnetics correction, fitted to this file's own reference netlist. The full netlist
stays in the source for calibration and is what the unit tests exercise (`reducedOrder = false` at the
top of the test).

