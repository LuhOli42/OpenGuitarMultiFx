# Boss OC-2-Style Octaver (analog octave-down)

The OC-2 is an ANALOG divide-down pedal, not a pitch shifter: it derives a square wave at the input
fundamental, divides it by 2 and 4 with flip-flops, and uses each divided square to chop the (lifted)
input signal at +/-0.5 -- an analog mixer that yields a note one, resp. two octaves below.

## Sources

- The OC-2 schematic as analysed by architolk's block diagram and the championleccy "sub octave" clone
  write-up: a x5 input amp (LP-filtered), an "analyser" chain of LP filter + peak-detector +
  comparators feeding a 4013 flip-flop divide-by-2 and a BA634 divide-by-4, then per-octave stages
  (OCT1/OCT2) where a 2SK30 JFET switch multiplies the lifted signal by +/-0.5 at the square rate,
  low-pass filtering, and the DIRECT/OCT1/OCT2 level mixer. 2SC1815/2SC732 helpers, 1S-188FM
  germanium diodes for the lift.

## What is modelled (behavioral-analog, block for block)

- Input coupling HPF (~20 Hz).
- **Analyser**: 2nd-order LP (~400 Hz) into dual diode peak detectors (envP/envN, ~8 ms release) whose
  midpoint + hysteresis band drives the comparator -- the same mechanism that tracks the fundamental
  and mis-tracks chords/decays exactly like the real pedal.
- **Dividers**: toggle flip-flops on the comparator's rising edges: f/2 and f/4 squares.
- **Octave creators**: signal lifted ~one peak above virtual ground (the diode lift), multiplied by
  +/-0.5 at the divided-square rate (the JFET chopper), then low-pass filtered (oct1: two LP sections
  ~800 Hz; oct2: the OCT1 output chopped again at f/4 then ~500 Hz sections -- the real OCT2 stage is
  fed from OCT1's output).
- **Mixer**: DIRECT, OCT1, OCT2 levels -- the three front-panel knobs.

## Deliberate simplifications

- Implemented as behavioral-analog blocks (Biquad + envelope/comparator state) rather than an
  NodalCircuit netlist: the heart of this pedal is logic (comparators, CMOS flip-flops, a JFET
  switched-chopper) which the MNA solver does not model; the linear filters it *can* model are
  represented by their exact transfer function instead.
- The germanium lift diode's exact knee and the analyser's attack/release RCs are approximated.
- Like the real unit, it is monophonic and tracks best in the bass register; chords produce the same
  glitches the analog circuit produces.
