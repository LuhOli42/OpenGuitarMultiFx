# Dr. Z Carmen Ghia-Style Amplifier

## Scope and documented facts

The manufacturer describes the Carmen Ghia as an 18 W amplifier with two EL84s, two 12AX7s, a 5Y3 rectifier, and Volume and Tone controls; a Master control was added in the 2024 version. The product page and the tube chart in the owner's manual disagree about the preamp tube complement: the website lists two 12AX7s, while the manual's table lists an EF86 and a 12AX7. This model follows the classic two-12AX7 design specified for this implementation.

Source: [Dr. Z Carmen Ghia](https://drzamps.com/carmen-ghia/)

## Modeled circuit

The estimated signal path is 12AX7 gain stage (100 kΩ plate load, 1.5 kΩ cathode resistor bypassed by 25 µF) → Volume → second 12AX7 gain stage (100 kΩ plate load and 1.5 kΩ unbypassed cathode resistor) → single-knob treble-cut Tone network → 12AX7 long-tailed-pair phase inverter → ganged 250 kΩ post-PI Master → two EL84s with shared 130 Ω / 250 µF cathode bias. All stage values, the treble-cut network, and Master implementation are **ESTIMATED**.

The power supply uses an estimated 5Y3-like source with higher series resistance and an estimated AC15-family output transformer and speaker load. The final output section omits global negative feedback (**ESTIMATED**). Apart from the product-level facts in the first section, all detailed circuit values and topology are **ESTIMATED**.

## Controls

Page 1: Volume, Tone, Master. Page 2: Power Drive, Bias, Tube Feel, Speaker, Output. Power Drive scales signal into the phase-inverter/output section; Bias, Tube Feel, Speaker, and Output are modeling controls.

## Verification

The focused processor suite converged and passed its silence, plucked-note, control-response, and reduced/full tracking checks. Measured DC operating values are implementation readings, not manufacturer specifications:

| Measurement | Voltage |
|---|---:|
| B+ | 340.0 V |
| Preamp plate | 196.4 V |
| PI plates | 332.7 / 332.7 V |
| Power plates | 394.3 / 394.3 V |
| Shared EL84 cathode | 14.9 V |

The measured default guitar-DI RMS was 0.345010 before registry trim, versus 0.061023 for AC30; the registry applies -15.05 dB to align defaults. The disagreement between the public tube listings is retained here rather than silently treated as resolved.
