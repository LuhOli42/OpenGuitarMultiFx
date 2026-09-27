# Overdriver-Style Overdrive

Display name **"Overdriver-Style Overdrive"**, registry key `OverdriverStyleOverdrive`, class `OverdriverStyleOverdriveProcessor`.
Runs on [`NodalCircuit`](./NodalCircuitSolver.md), **one block per channel, three real BJTs** (no op-amps, no diodes).
(The pedal is the Colorsound Overdriver, a Sola Sound product; the "-Style" convention applies, see `PositiveGroundBooster.md`.)

## Source
The maker's own drawing: **Sola Sound Ltd, drawing MD1066, issue 1, 14 June 1971** (schematicheaven.net fax). BC109 transistors, 9 V
battery. It marks the DC operating points of TR2 and TR3 (collector 5 V, emitter 1 V), which the tests check. The fax is noisy,
so the values below were each read twice at high zoom.

## What the circuit is
1. **TR1 + TR2, a directly coupled gain pair.** TR1's collector (120K to +9 V) *is* TR2's base. TR1's base gets the guitar through
   0.22 uF and its DC from **150K from TR2's emitter** (DC feedback that sets both stages' bias). TR1's emitter has 6.8K to ground,
   and in parallel 25 uF in series with the **Gain control, a 10K rheostat**: it changes the stage's AC gain, not its bias.
   TR2: collector 1.8K, emitter 470 ohm || 25 uF, **200 pF collector-base** (the top roll-off), 6.4 uF out. A **12K from TR2's output
   node back to TR1's emitter** is AC negative feedback that ties the pair's gain to resistor ratios.
2. **Baxandall tone network around TR3.** From TR2's output: 4.7K, then the **Bass pot (100K) with 0.1 uF across the whole
   pot**, 4.7K, to the network's far end "O". The Bass wiper goes through 39K, and the Treble wiper through 5.6K, to a common node
   which is TR3's input (0.1 uF to its base). Treble: 0.01 uF, the 100K pot, 0.01 uF, from TR2's output to "O".
   TR3 is a common-emitter stage (base 150K/33K, emitter 470 ohm || 25 uF, collector 1.8K) and **"O" returns to TR3's collector
   through 25 uF**: the network is TR3's feedback network, so it is an active Baxandall (boost and cut), not a passive
   stack that only loses signal.
3. 0.22 uF to the output. There is no volume control on the drawing.

The 0.1 uF across the Bass pot (the usual Baxandall has a capacitor at each side of the wiper) makes the Bass control act between
1/(2 pi 100K 0.1u) = 16 Hz and 1/(2 pi 9.4K 0.1u) = 170 Hz, so it moves the region around 30-120 Hz, mostly the lowest guitar notes.

## Model
* Every BJT is a real Ebers-Moll device (`addBjt`), `Is 1e-14, betaF 350`. betaF is an assumption (the BC109 spans 200-800).
* Pots: Gain is a linear rheostat (`10K (1 - gain)` in series with the 25 uF; clockwise = less resistance = more gain). Bass and
  Treble are 100K linear, clockwise = boost (the wiper toward TR2's end of the network); directions and tapers are assumptions and
  are checked against the network's response, not against the drawing.
* Output is loaded by 1 M.

## Verification (`Tests/OverdriverStyleOverdriveProcessorTests.cpp`)
* DC point: TR2 collector 5.26 V / emitter 0.98 V (drawing: 5 / 1); TR3 collector 5.87 V / emitter 0.82 V (drawing: 5 / 1: a real BC109 has a
  higher saturation current than `Is 1e-14`, which would bring it closer). Both inside the tolerance the test allows.
* Gain: 0.5 mV in gives 3.3 mV p-p at Gain 0 and 589 mV p-p at Gain 1 (the rheostat's full range is ~45 dB).
* Bass: at 50 Hz, Bass 1 is 33 dB above Bass 0 (relative to 1 kHz); Treble: at 6 kHz, 24 dB between the two ends.
* Hot input (3 V) and random knob motion: finite, solver failure rate 0, 1.24 Newton iterations/sample.
* Steady state is clean (-156 dB non-periodic error).

## Oversampling and cost
Alias (non-harmonic/harmonic, `PEDAL_ALIAS=1`): 1x -41.2 dB, 2x -65.0, 4x -72.6, 8x -73.0, so Orders {0, 1, 2}
(eco none, normal 2x, high 4x). Cost ~3.4% of a core at 1x (eco), ~6.9% at 2x. Unity trim -12.73 dB (measured with
`PedalUnityLevelTests`).
