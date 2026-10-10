# Mesa/Boogie Bass 400+

## What it is

Mesa's big bass head: 500 V of plate supply feeding twelve 6L6GCs (six per side) for ~250-400 W,
behind a two-channel preamp with a passive tone stack and the famous 7-band graphic equaliser.
Front panel: per channel Volume, Bass, Middle, Treble; Bass-Shift and Bright switches; Master;
EQ sliders at 40, 80, 160, 320, 750, 2200 and 6600 Hz; the EQ can be bypassed.

## What's modelled

- One channel of the real preamp order: two 12AX7 gain stages (150k/1.8k and 100k/1.8k,
  bypassed), the passive FMV-family tone stack (bass/mid/treble, audio-taper pots, Bright bleed
  cap), a recovery stage, the Volume and Master pots.
- The graphic EQ as its real topology, not a biquad bank: a series LC trap per band (L values
  decreasing with frequency, C = 1/(w^2 L)) from each band node to ground, both legs of every
  slider connecting the input/output of a feedback recovery gain block — the Mesa "feedback-LC"
  scheme where the boost side feeds back through the same network the cut side shunts. Modelled
  as a finite-gain op-amp plus real coupled inductors and caps in the netlist.
- The power amp, which is a straight SVT-family design: 12AX7 long-tailed-pair splitter on a
  negative tail, two 12AU7 common-cathode drivers, resistive level-shifters to ~-68 V of fixed
  bias, six 6L6GCs per side (composite Koren pentode, parallel-scaled), a ~2k plate-to-plate
  output transformer (coupled inductors), speaker impedance model, 100k global NFB into the
  second PI grid.
- The supply: ~500 V plate rail, ~400 V screen/driver, ~300 V preamp, each sagging under measured
  load — at twelve 6L6s the plate supply pulls down hard, which is most of the amp's feel.

## Honest simplifications

- Only channel 1 is modelled; channel 2's voicing differences are modest and the Input switch
  covers sensitivity.
- The Bass-Shift switch is folded into the Bright switch and the tone-stack cap values rather
  than being a separate control.
- The EQ inductor values are chosen to land the classic slider frequencies in-circuit (L from
  1 H at 40 Hz to 1 mH at 6.6 kHz); the real board's part numbers vary by revision.
- Slave-out / FX-return circuitry is omitted; the DI-out rear features don't affect the amp's
  own sound.

## Reduced-order power stage (shipped default)

Like the JCM800/SLO-100/Mark IIC+/Dual Rectifier/5150/Powerball/Rockerverb/Deluxe Reverb/AC30/Bassman/
Super Lead family (see CircuitFamilies.md), this processor ships a behavioural power stage as the
application default (`MesaBass400PlusStyleAmplifierProcessor::reducedOrder`, set centrally in EffectRegistry.cpp
together with `markQualityDependent`): the preamp and tone-shaping netlists remain real circuits,
while the phase-inverter + push-pull + output transformer + global NFB + speaker impedance cluster is
replaced by an envelope-followed sag rail feeding an asymmetric tanh knee, a DC blocker, and a
low-shelf speaker-magnetics correction, fitted to this file's own reference netlist. The full netlist
stays in the source for calibration and is what the unit tests exercise (`reducedOrder = false` at the
top of the test).
