# Bad Cat Hot Cat 30-Style Amplifier

## Scope and documented facts

This implementation models the classic 30 W, four-EL84, cathode-biased Hot Cat 30 as an **ESTIMATED** legacy variant rather than later Hot Cat 50/100 versions. The manufacturer's current site describes the later Hot Cat platform; a Hot Cat 30 manual or service schematic was not located in the research sources. Consequently, the complete circuit topology and component values below are **ESTIMATED**.

Sources:

- [Bad Cat Hot Cat product family](https://badcatamps.com/)
- [Bad Cat manuals and tube charts](https://badcatamps.com/manuals-and-tube-charts/)

## Modeled circuit

The estimated signal path is EF86 pentode input → Gain → 12AX7 partial-bypass gain stage → 12AX7 unbypassed gain stage → cathode follower → Marshall-style Treble/Middle/Bass network → Master → 12AX7 long-tailed-pair phase inverter → four EL84s modeled as two parallel pairs. The 30 W / 4×EL84 cathode-biased Class-A description, 5AR4 rectifier choice, no global negative feedback, and every resistor, capacitor, supply voltage, and transformer value are **ESTIMATED** for this legacy variant.

The power stage uses a shared 50 Ω / 250 µF cathode-bias network (**ESTIMATED**). The model includes a supply/sag network and output transformer/speaker load (**ESTIMATED**). The production reduced-order output uses an **ESTIMATED** 90 Hz one-pole high-pass to roll off subsonic energy. It deliberately does not borrow the later Hot Cat 50/100 EL34 design.

## Controls

Page 1: Gain, Bass, Middle, Treble, Master. Page 2: Power Drive, Bias, Tube Feel, Speaker, Output. There is no Reverb control or spring-reverb model.

## Verification

The focused processor suite checks DC convergence, silence, plucked-note stability, model controls, and reduced/full tracking. Measured DC operating values are implementation readings and are **ESTIMATED**, not manufacturer specifications:

| Measurement | Voltage |
|---|---:|
| B+ | 380.5 V (**ESTIMATED**) |
| Preamp plate | 196.4 V (**ESTIMATED**) |
| PI plates | 332.7 / 332.7 V (**ESTIMATED**) |
| Power plates | 395.3 / 395.3 V (**ESTIMATED**) |
| Shared EL84 cathode | 5.9 V (**ESTIMATED**) |

The re-rendered untrimmed default guitar-DI RMS is 0.042960 (**ESTIMATED**); the registry's +3.05 dB trim is derived by matching AC30's 0.061023 RMS reference (**ESTIMATED**). Because a factory schematic was unavailable, operating voltages and component values are model estimates rather than documented product specifications.
