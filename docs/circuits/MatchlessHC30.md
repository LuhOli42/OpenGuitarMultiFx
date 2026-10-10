# Matchless HC-30-Style Amplifier

## Scope and documented facts

This is a DC-30/C-30-family model used for the HC-30 head variant. Matchless describes Channel 1 as a parallel-triode 12AX7 channel with interactive Bass/Treble and Volume, Channel 2 as an EF86 channel with a six-position tone switch and Volume, and the shared Cut, Master, 12AX7 phase inverter and four-EL84 output section. The family is described as 30 W with a half-power option. The HC-30-specific product information is sparse, so the DC-30 circuit family is the topology reference.

Sources:

- [Matchless C-30](https://matchlessamplifiers.com/amplifiers-and-cabinets/c-30/)
- [Matchless HC-15](https://matchlessamplifiers.com/amplifiers-and-cabinets/hc-15/)
- [Matchless amplifier history](https://en.wikipedia.org/wiki/Matchless_Amplifiers)

## Modeled circuit

Channel 1 uses two parallel 12AX7 triodes, a shared 100 kΩ plate load, and an 820 Ω cathode resistor bypassed by 25 µF (**all component values ESTIMATED**). Its following Bass/Treble network is a simplified, estimated Top-Boost-style bridged RC network, followed by Volume 1 and a 12AX7 recovery stage (100 kΩ plate load, 1.5 kΩ unbypassed cathode resistor; **ESTIMATED**).

Channel 2 uses an EF86 pentode with a 220 kΩ plate load and 2.2 kΩ / 25 µF cathode network (**ESTIMATED**, based on the AC15-style pentode stage), followed by Volume 2 and a six-position shunt-cap tone control: open, 470 pF, 1 nF, 2.2 nF, 4.7 nF, and 10 nF (**ESTIMATED**). The default channel is the EF86 channel. The two channel paths feed the shared phase-inverter and power-stage model.

The shared phase inverter is a 12AX7 long-tailed pair. Four EL84s are modeled as two parallel pairs with shared 50 Ω / 250 µF cathode bias. The output transformer and speaker network, 400 V-class supply, rectifier sag, and screen behavior are **ESTIMATED**. The implementation omits global negative feedback (**ESTIMATED**). Cut is an estimated cross-PI control; Master is located before the PI.

## Controls

Page 1: Channel, Volume 1, Bass 1, Treble 1, Volume 2, Tone 2, Cut, Master. Page 2: Power Drive, Bias, Tube Feel, Speaker, Output.

Channel selects the parallel-triode or EF86 preamp. Tone 2 selects one of the six stepped capacitors. Page-2 Power Drive adjusts the signal driving the output section; Bias, Tube Feel, Speaker, and Output adjust the modeled operating point, sag, load, and final level respectively.

## Verification

The focused processor suite converged and passed its silence, plucked-note, control-response, and reduced/full tracking checks. Measured DC operating values are implementation readings, not manufacturer specifications:

| Measurement | Voltage |
|---|---:|
| B+ | 380.5 V |
| Preamp plate | 154.9 V |
| PI plates | 332.7 / 332.7 V |
| Power plates | 395.3 / 395.3 V |
| Shared EL84 cathode | 5.9 V |

The measured default guitar-DI RMS was 0.001617 before registry trim, versus 0.061023 for AC30; the registry applies +31.54 dB to align defaults. Component values beyond the manufacturer-level facts above are estimates, not claims about a factory schematic.
