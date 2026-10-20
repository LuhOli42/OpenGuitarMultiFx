# Octave-up fuzzes (Octavia, Green Ringer, Super-Fuzz, Tone Machine)

Processor: `Source/Effects/Pitch/OctaveFuzzProcessor.{h,cpp}` — keys `OctaviaStyleFuzz`, `GreenRingerStyleFuzz`,
`SuperFuzzStyleFuzz`, `ToneMachineStyleFuzz`. Sources: Justin Philpott's redraws (Roger Mayer Octavia, Univox Super-Fuzz,
Foxx Tone Machine), the annotated Unicord/Univox Super Fuzz drawing with DC voltages, and the Dan Armstrong Green Ringer
drawing (all in yuvadm/guitar-effects-schematics), plus a working Green Ringer's measured pin voltages (diystompboxes).

All four make the octave from a **phase splitter** (a transistor with equal collector and emitter resistors: the signal and
its inverse). What they do with the two halves differs, and that is most of their character. The core of each runs as a
netlist on `NodalCircuit`; the level, notch and tone networks around it are exact linear transfers.

| Pedal | Gain stage | Splitter | How the halves combine | Around it |
|---|---|---|---|---|
| Octavia | one stage, 39K / 3K9, Boost (10K) on the 22 u emitter bypass: x13 .. x180 | MPS-A13, 470K / 330K, 20K / 20K, behind .01 u | 22 u into two **1N4148**, anodes DC-fed from the splitter (2.2M from the collector, 820K from the emitter) so both idle a few uA forward; the higher half wins on 100K | .01 u into the Level (1M log) |
| Green Ringer | NPN, 560K / 160K, 6K2 emitter, 18K collector (x2.9), DC-coupled into the splitter | **PNP**, 10K / 10K, class A | .047 u into an NPN's two junctions (the "dual diode"), cathodes held at 4.5 V by 66K from a 22K / 22K divider, joined at the output follower's base (beta x 10K) | Level (synthetic) |
| Super-Fuzz | one stage, 470K collector-to-base feedback + 100K (0.8 V on the base, as marked), 47K / 1K8; follower = ideal buffer; Expander (Drive 50K) divides into the splitter | 220K / 150K, 10K / 10K | 10 u + 470R into **Q4 / Q5**: bases 100K / 22K (+ half the 10K Octave Balance), emitters joined on 1K8 (bypassed), collectors tied on 10K. Each is a common-emitter exponential driven in antiphase, so the summed current goes as **cosh(v / Vt)**: octave and compression in one | the tied collectors into a **1N34A** pair to ground (10K source), Normal / Notch (~1 kHz), Volume |
| Tone Machine | 2N3565, 47K / 1K, collector **straight into the splitter's base**; splitter emitter → 100K → 100K-grounded, 0.1 uF-decoupled midpoint → 47K → Q1 base; 1000 pF from Q1 base to splitter emitter | 4K7 / 4K7 | Collector and emitter outputs through 10 uF into 100K-grounded cathodes; the 1N34A anodes meet at nr, biased from +9 V through 100K. The collector diode is always connected; the Octave SPST connects the emitter-side diode anode to nr | nr → 10 uF → 50.22K Drive-pot load, then Sustain (50K into Q3, 10K / 1K bypassed: ~x150) into a 1N34A pair to ground behind 10K, Tone (3300p high leg vs 22K + 4K7 / .047 low leg), Volume |

## Operating points (prepare() solves them; checked against the drawings)
| Node | Model | Reference |
|---|---|---|
| Green Ringer Q1 b / e / c | 1.91 / 1.30 / 5.26 V | measured unit 1.8 / 1.4 / 5.2 |
| Green Ringer Q2 b / e / c | 5.26 / 5.88 / 3.11 V | measured unit 5.2 / 5.9 / 3.3 |
| Super-Fuzz splitter b / c / e | 3.56 / 6.07 / 2.94 V | drawing 3.6 / 6 / 3 |
| Super-Fuzz Q4/Q5 bases / emitters / collectors | 1.89 / 1.26 / 2.02 V | drawing 1.5-2.0 / 0.9-1.3 / 3.0-3.5 |
| Tone Machine Q1 base / collector; splitter collector / emitter; nr | 0.748 / 2.216 / 7.378 / 1.589 / 3.112 V | `debugNodeVoltage()` after `prepare()`; nr is the biased summing node |

With the corrected diode polarity and +9 V bias, the Tone Machine's 220 Hz test input still favors 440 Hz at both tested
levels: at default Sustain 0.6, the 440/220 amplitude ratio with Octave ON is **28.564 at 0.03 input** and **3.148 at
0.08**. With Octave OFF, the ratios are **0.001** and **0.035**, respectively. The 100K supply resistor biases both
diodes forward in the ON position; the emitter-side diode is opened by the switch when OFF. The Green Ringer was first
built from the redraw's device labels (a PNP input whose collector idles the splitter at 0.47 V, near cutoff): that gave
no octave at any level and ~40 dB too little output. The measured voltages show the labels are mirrored; with that
polarity the operating point lands within 0.2 V of the unit.

## Fidelity audit (2026-10)

Compared with Justin Philpott's Tone Machine drawing:
- Q1 bias now has the splitter-emitter 100K, grounded 100K midpoint, 0.1 uF bypass, and 47K midpoint-to-base resistor.
- Added the 1000 pF Q1-base-to-splitter-emitter capacitor.
- Reversed the rectifier diode orientation and moved nr's 100K bias to +9 V; the emitter diode is connected to nr by
  the Octave SPST switch.
- Added nr's 10 uF coupling capacitor into the 50.22K Drive-pot load before the existing Sustain/clipper abstraction.

## Simplifications (documented, not hidden)
- Octavia: the 2N3906 / MPS-A13 input feedback pair (gain set by the 10K "RLG" trimmer) is one stage with the same gain
  range; the MPS-A13 darlingtons are one junction with beta 5000.
- Super-Fuzz: the input pair is one stage plus an ideal follower, so the Expander pot is a Thevenin source (its wiper
  fraction, and f(1-f) x 50K in series) into a second netlist. The Octave Balance trimmer sits at its centre.
- Green Ringer: the output follower is its input resistance.
- Tone Machine and Super-Fuzz: the 1N34A clippers after the core are a solved shunt clipper from the 10K collector.
  The core's output is a collector (a current source), so it does not see that load.

## Oversampling and CPU (alias = non-harmonic / harmonic, 1567.4 Hz tone at 0.5, knobs 0.7)
| Pedal | 1x | 2x | 4x | tiers (eco / normal / high) | CPU at eco, noon, 220 Hz at 0.3 |
|---|---|---|---|---|---|
| Octavia | -36.1 dB | -40.8 | -40.9 | 2x / 2x / 4x | ~8% |
| Green Ringer | -35.3 | -56.4 | -61.9 | 2x / 2x / 4x | ~6% |
| Super-Fuzz | -12.5 | -20.7 | -28.3 | 2x / 4x / 8x | ~11% |
| Tone Machine | -13.7 | -21.6 | -28.9 | 2x / 4x / 8x | ~8% |

Tone Machine additionally measures **-35.7 dB at 8x**. None passes the -40 dB bar at 1x. The Super-Fuzz and Tone Machine
clip into germanium diodes after high gain (a near square), so like the Tone Bender each doubling buys only ~7-8 dB.
All four are above the 5% target, as the Fuzz Face, Tone Bender and Big Muff netlists are. Driven hard (knobs 0.7,
1.5 kHz) the eco cost roughly doubles (Octavia ~17%, Super-Fuzz ~20%).
A reduced-order pass (Big Muff style) is the next step if they need to be cheaper.
The Newton solve itself is cheap (1.0-1.7 iterations a sample); the cost is the size of the core and the oversampling.
