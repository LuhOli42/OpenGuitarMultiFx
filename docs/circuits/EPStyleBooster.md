# EP-Style Booster

Display name **"EP-Style Booster"**, registry key `EPStyleBooster`, class `EPStyleBoosterProcessor`. Runs on
[`NodalCircuit`](./NodalCircuitSolver.md), **one block per channel: one JFET, one BJT**. Listed under Overdrive in the UI with the
Rangemaster-style booster. (The circuit is the preamp of the Echoplex EP-3; the "-Style" convention applies, see `PositiveGroundBooster.md`.)

## Source
The Echoplex **EP-3 schematic diagram, serial no. 12961 to 28591** (schematicheaven.net `ep3_12961-28591.pdf`), read at 400 dpi. The
"EP booster" pedals copy this record preamp. **What is modelled** is the section from the input jack to Q3's collector; **not
modelled**: the echo return / sound-on-sound mix, Q4 and the record amplifier, the bias oscillator, the heads and the power supply
(the rail is an ideal 22 V, the value the drawing marks at its filtered node).

## What the circuit is
1. **Input**: 0.047 uF, 100K in series and 100 pF to ground, into the gate of **Q5 (TIS58 JFET)**, with 1M to ground. The 100K/100 pF is a
   low-pass near 16 kHz.
2. **Q5**: 22K drain load, **3.3K source, not bypassed** (so its gain is ~ gm 22K / (1 + gm 3.3K), a mild ~5x, and very linear). Marked bias:
   gate 0 V, source 1.1 V, drain 14.4 V.
3. **Record Level (500K)**: 0.1 uF from Q5's drain to the pot's top; the wiper goes through **47K**, and a **2 nF capacitor bypasses the
   pot's top to the far end of the 47K**, so above ~1.5 kHz the treble reaches the next stage no matter where the pot is.
4. **Q3 (2N3053 as drawn: a plain NPN)**: 0.1 uF into the base (47K to ground, **470K collector-base feedback**), emitter 1K || 100 uF,
   collector 22K, 0.1 uF out. The 470K feedback makes the input node a low-impedance summing node, so the stage's gain is set by
   ~470K over the source impedance the pot + 47K + 2 nF present. Marked bias: base 1.0 V, collector 12.2 V, emitter 0.4 V.

## The consequence worth knowing (verified in the tests, not an artifact)
Because the 2 nF bypasses the pot and its 47K, **Level barely moves the gain around 1 kHz and above (25.4 -> 28.5 dB over the whole knob)
but moves the low end by ~24 dB** (-0.9 to 23.5 dB at 100 Hz). Full Level therefore is a bass boost as much as a level control, and the
treble is always there (~+34 dB at 8 kHz): the EP-3 uses this as record pre-emphasis for the tape. This is what the drawing wires; if a
listening test shows the pedal is unusable as a general booster, that is the place to look (and the reading was re-checked at 3x zoom).

## Model
* Q5: `addJfet`, Idss 1.6 mA, pinch-off -2.0 V, lambda 0.02, **fitted so the model sits at the drawing's marked bias**; the TIS58's
  parameters spread widely and the drawing gives none.
* Q3: real Ebers-Moll, `Is 1e-14, betaF 150` (assumed).
* Record Level: 500K audio taper (`pots::audio`), clockwise = more. Taper and direction are assumptions.
* Output loaded by 1 M.

## Verification (`Tests/EPStyleBoosterProcessorTests.cpp`)
* DC point vs the marked voltages: Q5 drain 14.24 V (14.4), source 1.16 V (1.1); Q3 base 1.03 V (1.0), collector 12.64 V (12.2), emitter 0.40 V (0.4).
* Level monotonic at 100 Hz over a 24 dB range; the response at full Level: 30 Hz +4.8 dB, 300 Hz +27.4, 8 kHz +33.8.
* Hot input (3 V) and random Level moves: finite, solver failure rate 0, 1.07 iterations/sample.
* Steady state is clean (-449 dB).

## Oversampling and cost
Alias (`PEDAL_ALIAS=1`): 1x -52.7 dB, 2x -64.0, 4x -82.1, 8x -83.5, so Orders {0, 1, 1} (eco none, normal and high 2x). Cost ~1.9%
of a core. Unity trim -18.11 dB.
