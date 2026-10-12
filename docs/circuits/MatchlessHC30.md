# Matchless HC-30-Style Amplifier

## Scope and documented facts

This is a DC-30/C-30-family model used for the HC-30 head variant. Matchless describes Channel 1 as a parallel-triode 12AX7 channel with interactive Bass/Treble and Volume, Channel 2 as an EF86 channel with a six-position tone switch and Volume, and the shared Cut, Master, 12AX7 phase inverter and four-EL84 output section. The family is described as 30 W with a half-power option. The HC-30-specific product information is sparse, so the DC-30 circuit family is the topology reference.

Sources:

- [Matchless C-30](https://matchlessamplifiers.com/amplifiers-and-cabinets/c-30/)
- [Matchless HC-15](https://matchlessamplifiers.com/amplifiers-and-cabinets/hc-15/)
- [Matchless amplifier history](https://en.wikipedia.org/wiki/Matchless_Amplifiers)

## Modeled circuit

Channel 1 uses two parallel 12AX7 triodes, a shared 100 kΩ plate load, and an 820 Ω cathode resistor bypassed by 25 µF (**all component values ESTIMATED**). Its Top-Boost-style Bass/Treble stack reuses the AC30 implementation's 50 pF coupling capacitor, 100 kΩ slope resistor, two 0.022 µF capacitors, 1 MΩ Treble and Bass tracks, and 10 kΩ ground leg (**ESTIMATED**); the plate feeds the stack rather than bypassing it to the Volume 1 wiper. Volume 1 feeds a 12AX7 recovery stage (100 kΩ plate load, 1.5 kΩ unbypassed cathode resistor; **ESTIMATED**).

Channel 2 uses an EF86 pentode with a 220 kΩ plate load and 2.2 kΩ / 25 µF cathode network (**ESTIMATED**, based on the AC15-style pentode stage), followed by Volume 2, a 100 kΩ series resistor, the six-position tone-cap shunt, a 47 kΩ series resistor, and the 1 MΩ load (**ESTIMATED**). Tone positions are open, 470 pF, 1 nF, 2.2 nF, 4.7 nF, and 10 nF (**ESTIMATED**). The default channel is Channel 1; both paths feed the shared phase-inverter and power-stage model.

The shared phase inverter is a 12AX7 long-tailed pair. Four EL84s are modeled as two parallel pairs with shared 50 Ω / 250 µF cathode bias (**ESTIMATED**). The output transformer and speaker network, 400 V-class supply, rectifier sag, and fixed screen voltage are **ESTIMATED**. The nodal pentode API accepts screen voltage as a per-sample scalar, not a circuit node, so no synthetic screen-voltage probe is exposed. The implementation omits global negative feedback (**ESTIMATED**). Cut is an estimated cross-PI control; Master is located before the PI. The production reduced-order output uses an **ESTIMATED** 90 Hz one-pole high-pass to roll off subsonic energy.

## Controls

Page 1: Channel, Volume 1, Bass 1, Treble 1, Volume 2, Tone 2, Cut, Master. Page 2: Power Drive, Bias, Tube Feel, Speaker, Output.

Channel selects the parallel-triode or EF86 preamp. Tone 2 selects one of the six stepped capacitors. Page-2 Power Drive adjusts the signal driving the output section; Bias, Tube Feel, Speaker, and Output adjust the modeled operating point, sag, load, and final level respectively.

## Verification

The focused processor suite checks DC convergence, silence, plucked-note stability, model controls, and reduced/full tracking. Measured DC operating values are implementation readings and are **ESTIMATED**, not manufacturer specifications:

| Measurement | Voltage |
|---|---:|
| B+ | 380.5 V (**ESTIMATED**) |
| Preamp plate | 154.9 V (**ESTIMATED**) |
| PI plates | 332.7 / 332.7 V (**ESTIMATED**) |
| Power plates | 395.3 / 395.3 V (**ESTIMATED**) |
| Shared EL84 cathode | 5.9 V (**ESTIMATED**) |

The original +31.54 dB trim came from treating the quieter EF86 channel as the default: at 220 Hz Channel 2 measured 0.000778 Vrms versus 0.023700 Vrms on Channel 1 (29.7 dB lower). The erroneous Channel 1 plate-to-Volume-wiper bypass was also removed, and its Top-Boost-like stack reuses the AC30 implementation's values listed above. The re-rendered untrimmed default DI RMS is 0.079141 (**ESTIMATED**); the registry's -2.26 dB trim is derived by matching AC30's 0.061023 RMS reference (**ESTIMATED**). Component values beyond the manufacturer-level facts above are estimates, not claims about a factory schematic.
