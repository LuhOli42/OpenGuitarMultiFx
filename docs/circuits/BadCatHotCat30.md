# Bad Cat Hot Cat 30-Style Amplifier

## Scope and documented facts

This implementation models the classic 30 W, four-EL84, cathode-biased Hot Cat 30 rather than later Hot Cat 50/100 versions. The manufacturer's current site describes the later Hot Cat platform; a Hot Cat 30 manual or service schematic was not located in the research sources. Consequently, the complete circuit topology and component values below are **ESTIMATED**.

Sources:

- [Bad Cat Hot Cat product family](https://badcatamps.com/)
- [Bad Cat manuals and tube charts](https://badcatamps.com/manuals-and-tube-charts/)

## Modeled circuit

The estimated signal path is EF86 pentode input → Gain → 12AX7 partial-bypass gain stage → 12AX7 unbypassed gain stage → cathode follower → Marshall-style Treble/Middle/Bass network → Master → 12AX7 long-tailed-pair phase inverter → four EL84s modeled as two parallel pairs. The 30 W / 4×EL84 cathode-biased Class-A description, 5AR4 rectifier choice, no global negative feedback, and every resistor, capacitor, supply voltage, and transformer value are **ESTIMATED** for this legacy variant.

The power stage uses a shared 50 Ω / 250 µF cathode-bias network (**ESTIMATED**). The model includes a supply/sag network and output transformer/speaker load (**ESTIMATED**). It deliberately does not borrow the later Hot Cat 50/100 EL34 design.

## Controls

Page 1: Gain, Bass, Middle, Treble, Master. Page 2: Power Drive, Bias, Tube Feel, Speaker, Output. There is no Reverb control or spring-reverb model.

## Verification

The focused processor suite converged and passed its silence, plucked-note, control-response, and reduced/full tracking checks. Measured DC operating values are implementation readings, not manufacturer specifications:

| Measurement | Voltage |
|---|---:|
| B+ | 380.5 V |
| Preamp plate | 196.4 V |
| PI plates | 332.7 / 332.7 V |
| Power plates | 395.3 / 395.3 V |
| Shared EL84 cathode | 5.9 V |

The measured default guitar-DI RMS was 0.168719 before registry trim, versus 0.061023 for AC30; the registry applies -8.83 dB to align defaults. Because a factory schematic was unavailable, operating voltages and component values are model estimates rather than documented product specifications.
