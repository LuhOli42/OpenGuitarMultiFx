# Soldano Astroverb 16-Style Amplifier

## Scope and documented facts

The manufacturer's legacy specification describes a single-channel amplifier with two EL84s, five 12AX7s, Preamp, Reverb, Bass, Middle, Treble, Volume, and Presence controls, a tube-driven spring reverb, a 4–16 Ω output range, and a “20 watts” specification despite the Astroverb 16 name.

Source: [Soldano Astroverb 16 / 20 W head](https://soldano.com/products/legacy/astroverb-16-20w-head/)

The modeled scope is only the clean signal-amplification path. The spring tank and its two 12AX7 halves are omitted; there is no Reverb parameter.

## Modeled circuit

The estimated path is a cascaded SLO-style 12AX7 preamp → cathode follower → Treble/Middle/Bass network → Volume → 12AX7 long-tailed-pair phase inverter → two EL84s with shared 130 Ω / 250 µF cathode bias. Global negative feedback is taken from the estimated 8 Ω secondary through an estimated 47 kΩ resistor to the PI tail return. Presence is implemented as an estimated control in the feedback path. The detailed stage values, supply, transformer, and speaker model are **ESTIMATED**.

Although the product specification includes spring reverb, this model intentionally omits the reverb circuitry and its two tube halves. It does not expose a Reverb control and must be paired with a separate reverb effect when that sound is wanted.

## Controls

Page 1: Preamp, Bass, Middle, Treble, Volume, Presence. Page 2: Power Drive, Bias, Tube Feel, Speaker, Output. No Reverb control is included.

## Verification

The focused processor suite converged and passed its silence, plucked-note, control-response, and reduced/full tracking checks. Measured DC operating values are implementation readings, not manufacturer specifications:

| Measurement | Voltage |
|---|---:|
| B+ | 380.5 V |
| Preamp plate | 153.1 V |
| PI plates | 332.2 / 332.2 V |
| Power plates | 394.3 / 394.3 V |
| Shared EL84 cathode | 14.9 V |

The measured default guitar-DI RMS was 0.414010 before registry trim, versus 0.061023 for AC30; the registry applies -16.63 dB to align defaults. Only the high-level product facts in the first section are manufacturer-documented; detailed circuit values and NFB implementation are estimates.
