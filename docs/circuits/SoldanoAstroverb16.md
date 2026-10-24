# Soldano Astroverb 16-Style Amplifier

## Scope and documented facts

The manufacturer's legacy specification describes a single-channel amplifier with two EL84s, five 12AX7s, Preamp, Reverb, Bass, Middle, Treble, Volume, and Presence controls, a tube-driven spring reverb, a 4–16 Ω output range, and a “20 watts” specification despite the Astroverb 16 name.

Source: [Soldano Astroverb 16 / 20 W head](https://soldano.com/products/legacy/astroverb-16-20w-head/)

The modeled scope is only the clean signal-amplification path. The spring tank and its two 12AX7 halves are omitted; there is no Reverb parameter.

## Modeled circuit

Preamp from the factory schematic ([El34 World: Soldano_astroverb_16.pdf](https://el34world.com/charts/Schematics/files/Soldano/Soldano_astroverb_16.pdf)), rebuilt 2026-10-24:

- jack -> 69k grid stopper (1M to ground) -> **V1a** (220k plate, 1k8 + 1 uF cathode; drawing: 134 V / 1.0 V);
- 22 nF -> 1M -> **PREAMP** 1M pot to ground, wiper -> **V1b** (100k plate, 2k2 unbypassed; 182 V / 1.6 V);
- 22 nF -> 1M bridged by 22 nF -> 220k -> **V2a** grid (330k leak), V2a 220k plate, 1k8 + 1 uF (187 V / 1.5 V);
- 22 nF -> **680k / 15k divider** -> **V2b**, paralleled with **V3a** (the reverb-return mixer; its grid sits on the
  250k REVERB pot, the reverb itself is omitted) on a shared 100k plate and 1k + 100 uF cathode (152 V / 1.2 V);
- DC-coupled **V3b** cathode follower, plate on the 272 V node -> Treble 250k / Bass 1M / Middle 25k stack -> Volume.
- Supply chain: B+ 360 V -> 1k -> 348 V (screens) -> 10k -> 272 V (33 uF: PI, V3b) -> 10k -> 258 V (33 uF: preamp).
  The model feeds the preamp from the supply's filtered rail (x 0.725) and the 272 V node from the same rail x 272/258.

Read as drawing errors, kept as estimates: the "1k" at V3b's cathode (it could not carry its 153 V; the usual 100k is
used), the "1k" slope and Volume-network resistors (the earlier estimates stay: 33k slope, 10k + 1M Volume). A
Music-Electronics-Forum repair thread notes this drawing's values do not all match the boards.

Before this rebuild the preamp was an estimated five-stage 220k cascade with a 10k / 100 uF "cold clipper" that is not
in the amp. Its excess gain made the model motorboat through the supply (output wandering by volts with no input) and the
preamp solver storm (15 % of samples failed on a plucked note, Preamp control with no audible effect). With the drawn
circuit: silence stays at 0, no solver failures, reduced/full within 0.6 dB, the Preamp control works.

The power section is still the estimated one: two EL84s with shared 130 ohm / 250 uF cathode bias and NFB from the
secondary through 47k. The real amp is fixed-bias (-16 V grids, 2k2 stoppers, 470R + 10k elevated PI reference, NFB
10k -> FB -> 4k7 to the tail); that rework stormed the full-order solver when it was tried in #111 and is still open.

The spring tank and its two 12AX7 halves (V4) are omitted; there is no Reverb parameter.

## Controls

Page 1: Preamp, Bass, Middle, Treble, Volume, Presence. Page 2: Power Drive, Bias, Tube Feel, Speaker, Output. No Reverb control is included.

## Verification

The focused processor suite converged and passed its silence, plucked-note, control-response, and reduced/full tracking checks. Measured DC operating values are implementation readings, not manufacturer specifications:

| Measurement | Voltage |
|---|---:|
| B+ | 380.5 V |
| Preamp plate (V1a) | 153.1 V (drawing: 134 V on a 258 V node; the model's node runs ~20 V higher) |
| PI plates | 332.2 / 332.2 V |
| Power plates | 394.3 / 394.3 V |
| Shared EL84 cathode | 14.9 V |

The measured default guitar-DI RMS was 0.414010 before registry trim, versus 0.061023 for AC30; the registry applies -16.63 dB to align defaults. Only the high-level product facts in the first section are manufacturer-documented; detailed circuit values and NFB implementation are estimates.
