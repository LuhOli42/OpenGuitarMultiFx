# Acoustic 360

## What it is

The solid-state bass rig Acoustic sold as the 360 head + the folded-horn 361 cabinet. The 360 is a **preamp only**
(six NPN transistors on +25 V, plus a fuzz and an "ETF" section); the 200 W power amp lives inside the 361.

## Source

Acoustic Control Corp's own drawings in the 360/361 service manual (ozvalveamps.org `acoustic-service-manual-360-361.pdf`,
36 pages, linked from the unofficial Acoustic forum's schematic index): **"MODEL 360 SCHEMATIC" 1006A** (rev 7/10/70) and
**"SCHEMATIC MODEL 361" 1005A**. Rebuilt from them on 2026-10-25; the model before was an estimate (two generic stages, a
0.5 H Variamp tuned 390-3900 Hz that could only cut, +-55 V direct-coupled power stage).

## What's modelled (360, 1006A)

- **Q1** (MPSA09) bootstrapped emitter follower: R1 22K in, C2 1.0, R3 1M (100 pF across) / R4 4.7M bias off +25 V,
  R5 100K emitter, C4 1.0 out, R6 470K.
- **VOLUME** is the feedback ratio of the inverting **Q2** stage: the 50K pot's input end at Q1's output, its wiper into
  Q2's base (C6 1.0), its far end through R9 8.2K + C7 1.0 to Q2's collector (R11 10K, R12 680 emitter, R10 2.7M
  collector-base bias). **BRIGHT** (S1): C5 .01 + R8 2.2K from the input to the wiper.
- **Q3** follower (R13 10K) -> C8 1.0 -> **Q4** (R14 270K collector-base, R15 680, R16 3.9K).
- **TREBLE / BASS**: C40 .0025 to the 25K treble pot, R17 10K, C10 1.0, the 25K bass pot as a rheostat, C11 .47, R20 680;
  the treble wiper -> C12 1.0 -> Q5.
- **Q5** (R21 1M collector-base, R22 4.7K, R23 2.2K) with the **VARIAMP**: R24 50K between the collector (C14 6.8) and the
  emitter (R25 680 + C13 6.8); its wiper drives R26 100K and the series **L1 1.5 H + RANGE cap** (C15 2.2 / C16 .47 /
  C17 .1 / C18 .027 uF = 87 / 190 / 411 / 790 Hz) to ground. Wiper toward the collector cuts the band, toward the emitter
  boosts it, centre is flat. (Panel: Variamp = the depth pot, 0 cut .. 0.5 flat .. 1 boost; the RANGE rotary has 4
  positions.)
- **Q6** follower (R27 10K) -> C19 1.0 -> R28 8.2K -> R29 50K (kept at max) -> the 361.

## The 361 power amp (1005A), full reference path

Input C2 .47 -> R2 10K -> C3 .1 -> R35 1K into the 2N4248 pair (R3 47K bias); NFB R11 100K over R9 470 (+ R10 100K /
C6 10 uF): ~214x. Single +80 V rail (C1 7800 uF) swinging +-40 V around half-rail, eight 40411s quasi-complementary,
the speaker through C9 3900 uF. Modelled as a saturating gain block with those networks and rails.

## Honest simplifications

- The fuzz (17-2A: Q7-Q10, Gain / Attack) and the ETF section (17-4) are not modelled.
- The 361's discrete output stage is a saturating op-amp at its rails, not a transistor-level model; its clipping is
  dominated by the rails anyway.
- R29 (rear output level) is fixed at maximum.

## Reduced-order power stage (shipped default)

Like the JCM800/SLO-100/Mark IIC+/Dual Rectifier/5150/Powerball/Rockerverb/Deluxe Reverb/AC30/Bassman/
Super Lead family (see CircuitFamilies.md), this processor ships a behavioural power stage as the
application default (`Acoustic360StyleAmplifierProcessor::reducedOrder`, set centrally in EffectRegistry.cpp
together with `markQualityDependent`): the preamp and tone-shaping netlists remain real circuits,
while the phase-inverter + push-pull + output transformer + global NFB + speaker impedance cluster is
replaced by an envelope-followed sag rail feeding an asymmetric tanh knee, a DC blocker, and a
low-shelf speaker-magnetics correction, fitted to this file's own reference netlist. The full netlist
stays in the source for calibration and is what the unit tests exercise (`reducedOrder = false` at the
top of the test).
