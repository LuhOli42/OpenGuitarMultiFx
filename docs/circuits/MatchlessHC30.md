# Matchless HC-30-Style Amplifier

## Scope and documented facts

This is a DC-30/C-30-family model used for the HC-30 head variant. Matchless describes Channel 1 as a parallel-triode 12AX7 channel with interactive Bass/Treble and Volume, Channel 2 as an EF86 channel with a six-position tone switch and Volume, and the shared Cut, Master, 12AX7 phase inverter and four-EL84 output section. The family is described as 30 W with a half-power option. The HC-30-specific product information is sparse, so the DC-30 circuit family is the topology reference.

Sources:

- [Matchless C-30](https://matchlessamplifiers.com/amplifiers-and-cabinets/c-30/)
- [Matchless HC-15](https://matchlessamplifiers.com/amplifiers-and-cabinets/hc-15/)
- [Matchless amplifier history](https://en.wikipedia.org/wiki/Matchless_Amplifiers)
- DC-30 trace: el34world `Matchless/Matchless_dc30_old.pdf` (see docs/circuits/SchematicSources.md)

## Modeled circuit

From the DC-30 trace on el34world (`Matchless/Matchless_dc30_old.pdf`, four hand-drawn sheets: Ch1, Ch2, PI/power, supply;
Elektrotanya's "DC-30" is the same drawing). It is a builder's trace, not a factory drawing; the values below are read from
it, the rest are marked.

- **Channel 1**: two paralleled 12AX7 triodes (68k grid stoppers, 1M leak, shared 220k plate; cathode 25 uF + a resistor the
  trace leaves illegible -- 820R kept), 560 pF -> VOL 500kA (180 pF bright) -> V2a (100k, 1k2 + 25 uF) -> V2b cathode
  follower (56k) -> **Fender-style Treble/Bass stack**: 56 pF to TREBLE 1MA, 18k slope, .022 to the treble-bottom / bass-top
  node, .022 to the BASS 1MA wiper (bass bottom to ground, no mid pot); the output leaves the treble wiper through 220k into
  18k to ground -> PI grid A. (Until 2026-10-25 this was two parallel cap/pot legs with a 220k treble pot and a 180k load,
  and a 47k follower.)
- **Channel 2**: EF86 (68k grid stopper, 330k plate, screen through **2M2** with 0.1 uF -- the model tracks the screen
  voltage from the screen current, tau 0.22 s -- 2k2 + 25 uF cathode). **TONE** selects the *series* coupling cap from the
  plate to VOL 1MA: 56 pF, 360 pF, .0012, .0022, .0047 or .01 + 180 pF (factory-added), each with a 5M bleed (default: the
  full .01 position); 180 pF bright on the volume; the wiper with 470k to ground -> PI grid B. (Until 2026-10-25: 220k
  plate, a fixed 200 V screen, and the tone caps as shunts after a fixed 3.6 nF.)
- **PI / power**: 12AX7 long-tailed pair with the grids returned through 1M each to an elevated reference (1k2 to the
  cathodes, 47k to ground), 100k plates, .047 outputs; **CUT 250kA + .0022** across the two PI outputs (was 500k + 4n7);
  220k grid leaks, 1k5 grid stoppers, 100R screen stoppers; four EL84 on a shared 68R / 250 uF cathode (62R on Lo power).
- Supply: 350 / 338 / 308-320 V nodes through 22k / 4k7 droppers (the trace's measured voltages); the model keeps its
  rectifier + choke supply and the preamp tap at 0.725 B+.

Not modelled: the Master's real place (a pull pot after the PI; the model scales the PI drive), the FX loops, Hi/Lo power.

## Controls

Page 1: Channel, Volume 1, Bass 1, Treble 1, Volume 2, Tone 2, Cut, Master. Page 2: Power Drive, Bias, Tube Feel, Speaker, Output.

Channel selects the parallel-triode or EF86 preamp. Tone 2 selects one of the six series coupling capacitors (thinnest first). Page-2 Power Drive adjusts the signal driving the output section; Bias, Tube Feel, Speaker, and Output adjust the modeled operating point, sag, load, and final level respectively.

## Verification

The focused processor suite converged and passed its silence, plucked-note, control-response, and reduced/full tracking checks. Measured DC operating values are implementation readings, not manufacturer specifications:

| Measurement | Voltage |
|---|---:|
| B+ | 380.5 V |
| Preamp plate | 154.9 V |
| PI plates | 332.7 / 332.7 V |
| Power plates | 395.3 / 395.3 V |
| Shared EL84 cathode | 5.9 V |

The measured default guitar-DI RMS was 0.001617 before registry trim, versus 0.061023 for AC30; the registry applies +31.54 dB to align defaults. Values come from the DC-30 trace above; the illegible ones are marked.
