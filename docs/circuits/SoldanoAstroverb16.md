# Soldano Astroverb 16-Style Amplifier

## Scope and documented facts

The manufacturer's legacy specification describes a single-channel amplifier with two EL84s, five 12AX7s, Preamp, Reverb, Bass, Middle, Treble, Volume, and Presence controls, a tube-driven spring reverb, a 4–16 Ω output range, and a “20 watts” specification despite the Astroverb 16 name.

Source: [Soldano Astroverb 16 / 20 W head](https://soldano.com/products/legacy/astroverb-16-20w-head/)

The modeled scope is only the clean signal-amplification path. The spring tank and its two 12AX7 halves are omitted; there is no Reverb parameter.

## Modeled circuit

The estimated path is a cascaded SLO-style 12AX7 preamp → cathode follower → Treble/Middle/Bass network → Volume → 12AX7 long-tailed-pair phase inverter → two EL84s with shared 130 Ω / 250 µF cathode bias. Global negative feedback is taken from the estimated 8 Ω secondary through an estimated 47 kΩ resistor to the PI tail return. Presence is implemented as an estimated control in the feedback path. The detailed stage values, supply, transformer, and speaker model are **ESTIMATED**.

Although the product specification includes spring reverb, this model intentionally omits the reverb circuitry and its two tube halves. It does not expose a Reverb control and must be paired with a separate reverb effect when that sound is wanted. Both full and reduced processing use one **ESTIMATED** 100 Hz fourth-order Butterworth output high-pass; the additional cascaded 100 Hz sections used during diagnosis were removed.

## Controls

Page 1: Preamp, Bass, Middle, Treble, Volume, Presence. Page 2: Power Drive, Bias, Tube Feel, Speaker, Output. No Reverb control is included.

## Verification

The focused processor suite checks DC convergence, silence, plucked-note stability, model controls, and reduced/full tracking. Measured DC operating values are implementation readings and are **ESTIMATED**, not manufacturer specifications:

| Measurement | Voltage |
|---|---:|
| B+ | 380.5 V (**ESTIMATED**) |
| Preamp plate | 153.1 V (**ESTIMATED**) |
| PI plates | 332.6 / 332.6 V (**ESTIMATED**) |
| Power plates | 394.3 / 394.3 V (**ESTIMATED**) |
| Shared EL84 cathode | 14.9 V (**ESTIMATED**) |

The untrimmed default guitar-DI RMS is 0.561143 (**ESTIMATED**); the registry's -19.27 dB trim matches AC30's 0.061023 RMS reference (**ESTIMATED**). Render measurements below use the guitar DI; all are **ESTIMATED**.

| DI render | RMS after trim | Crest | <80 Hz |
|---|---:|---:|---:|
| Default | 0.061034 | 14.09 dB | -40.78 dB |
| Clean (Preamp = 0.2) | 0.047881 | 14.36 dB | -44.98 dB |
| Hot (Preamp = 0.7, Volume = 0.8) | 0.166952 | 15.40 dB | -39.18 dB |

The hot reduced-order behavioral speaker-node peak is 29.64 V (**ESTIMATED**); the corresponding full-netlist hot peak is 32.73 V (**ESTIMATED**). The behavioral stage uses an **ESTIMATED** 0.09 speaker-node scale with inverse post-guard compensation, preventing guard-induced DC/LF while preserving output tracking. Only the high-level product facts in the first section are manufacturer-documented; detailed circuit values and NFB implementation are estimates.
