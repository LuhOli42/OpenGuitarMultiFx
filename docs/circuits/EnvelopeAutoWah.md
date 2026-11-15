# Envelope Auto-Wah (MXR Envelope Filter, MXR Bass Envelope Filter, Doctor Q)

`EnvelopeAutoWahProcessor` -- one class, three circuits. An auto-wah is a resonant band-pass filter whose centre
frequency is moved by an **envelope follower on the guitar**, so the wah opens when you play hard and closes as the note
decays: a wah pedal played by the picking dynamics instead of a foot. (Not the Cry Baby family -- that is a foot pedal
with an inductor; the Mu-Tron III, the other envelope filter in the project, is [MuTronIIIStyleFilter.md](MuTronIIIStyleFilter.md).)

Every model also has **Mode: Up / Down / LFO** and **Rate**: Down inverts the envelope (loud closes the wah), LFO replaces
the envelope with a triangle so the wah sweeps by itself ("wah-wah" on its own). Those three are additions to the real
panels.

## 1. MXR Envelope Filter (schematic 120-4001, 1979)

Source: the MXR drawing in `yuvadm/guitar-effects-schematics` (Filters Wahs and VCFs). Read by hand from the scan.

**Filter.** A state-variable filter built from three CMOS inverters used as op-amps:

- stage 1 (summer / high-pass): `HP = -1.2 (x + LP)` -- R-13 240K feedback over R-7 / R-12 200K;
- stage 2 (band-pass integrator): input R-18 47K through a CD4016 switch, feedback C-7 1 nF **in parallel with R-19 200K** (the leak sets the damping, so Q);
- stage 3 (low-pass integrator): R-25 47K through the second switch, feedback C-6 1 nF.

The two switches are the tuning resistors. They are chopped at 37 kHz by a PWM signal, so over an audio cycle each
behaves as `R / duty` (plus the switch's ~400 ohm on-resistance). Natural frequency
`f0 = sqrt(1.2) * duty / (2 pi (47K + Ron) * 1 nF)` -> 3.4 kHz x duty. Q = `w0 * R19 * C7`, so **Q rises with the sweep**
(about 0.4 at rest, about 4.7 at the top): the quack sharpens as the wah opens.

The output is the **band-pass** tap. Evidence: the designer's handwritten note on the schematic gives the resonance as
"~280 Hz low, 2.8-3.2 kHz high"; with duty 0.08 the band-pass peaks at 294 Hz and with duty 0.87 at 3.2 kHz, while the
low-pass has no peak at the bottom of the sweep. Those two duties (`dutyMin` / `dutyMax`) are the only fitted numbers.

**Detector.** Input inverter 5.1M / 1M (gain 5.1) -> **Threshold** stage `470K / (10K + 500K pot)` (gain 47 .. 0.9) -> diode
peak detector -> comparator against the 37 kHz triangle, so duty is linear in the envelope voltage (full-scale ~1.6 V).
The pot is an audio taper (`PotTaper.h`).

*Estimated (the Attack stage's component values are not legible on the scan):* the detector's Attack is a 1-60 ms one-pole
and Decay a 40 ms-0.6 s one-pole, exposed as knobs. The real unit's release comes from fixed parts. Peak (Q) varies R-19
50K-1M around the 200K of the schematic; the real unit has no such knob.

**Solver.** The SVF is solved in closed form per sample (trapezoidal rule on the two integrators, no iteration): with
`tau = T/2`, `a = duty / ((R+Ron) C)`, `gamma = 1/(R19 C)`,
`HP = (-1.2 (x + P) + 1.2 tau a Qb/(1+tau gamma)) / (1 + 1.2 tau^2 a^2 /(1+tau gamma))`,
`BP = (Qb - tau a HP)/(1+tau gamma)`, `LP = P - tau a BP`, with `P`, `Qb` the previous states plus tau x their slopes. The
37 kHz chopping is represented by its average. The CMOS stages rail softly at +-2.1 V. Input and output coupling
(.05 uF into 200K, 16 Hz) are one-pole high-passes.

## 2. MXR Bass Envelope Filter (M82-style)

The bass version is the MXR chassis with two changes, taken from the project's own description of the M82
([BassGear.md](../BassGear.md): "auto-wah state-variable filter swept by picking dynamics; Dry/FX blend preserves lows"):

- integrator caps 3.3 nF instead of 1 nF -> the sweep runs 3.3x lower (about 90 Hz .. 1 kHz), so a bass fundamental
  actually lands inside it;
- a **Dry/FX** blend (default 0.6) so the low fundamental is never filtered away.

*Not from a schematic*: the M82's own drawing is not in the schematic collection; this is the MXR circuit re-voiced, and
the doc says so rather than pretending otherwise.

## 3. Doctor Q-style (Electro-Harmonix Doctor Q)

Source: the EHX Doctor Q drawing in the same collection (legible, values clear).

**Filter.** A **multiple-feedback band-pass** op-amp stage. The inverting input is a virtual earth; node N has the two
.005 uF caps (one to the inverting input, one to the output), the 470K feedback resistor from output to the inverting
input, a 47K, and a **BC239C transistor used as the tuning resistor to earth**. With the transistor as `Rv`:
`w0^2 = (1/47K + 1/Rv) / (470K * C^2)`, C = 5 nF. `Rv` = 10K -> ~510 Hz, 1K -> ~1.5 kHz, 100 ohm -> ~4.6 kHz.

The transistor sits at ~0 V collector-emitter, where a bipolar transistor is a base-current-controlled resistor,
`Rce ~ VT / (betaF * Ib)`. The model floors it at 120 ohm (bulk resistance, **estimated**) and caps it at 2 Mohm
(leakage). It is treated as linear: at very low `Ib` the 47K divider would put tens of mV across it and it is not perfectly
linear there in the real part -- accepted.

**Norm / Bass switch.** The DPDT moves the signal between two entry points of the same network:

- **Norm**: input -> 47K -> node N (the classic MFB: a true band-pass, lows rolled off);
- **Bass**: input -> .01 uF -> 470K -> the inverting input (virtual earth). Solving the network, the low-frequency gain
  becomes `-Yin / Gf = -1` -- **flat lows with the wah peak riding on top**, instead of a band-pass that throws the bass
  away. That is the circuit's own bass voicing, which is why it is a switch here and not a separate model.

*Reading note*: which throw is labelled Norm vs Bass is not printed unambiguously on the scan; assigned by what each position
does (the one with flat lows is Bass). The two op-amp-fed 47 ohm / 9 V pins and the .001 uF cap to earth only matter with
a finite-bandwidth op-amp and are left out.

Measured (quiet probe, top of the sweep): Norm -48 dB at 100 Hz vs 0 dB peak band; Bass +6.8 dB at 100 Hz (flat region)
with a +32 dB peak -- the unit test pins both.

**Detector.** Sensitivity pot (100K, audio taper) -> 47K -> .05 uF -> inverting amp 2.2M/47K (gain up to 47) -> 1N914 ->
100R + 10 uF storage -> 1N914 -> 25K Range trim + 22K -> base. Charging is through ~120 ohm (about 1.2 ms); the 10 uF discharges
through the base network (`Ib = (V - 1.25 V) / (22K + trim)`, where 1.25 V is the diode plus the base-emitter drop), so
there is a **threshold** below which the wah does not move, and the release is the real RC (0.2-0.5 s), not a knob.
Range is the real trimmer.

**Solver.** Trapezoidal companion models for the three capacitors (the two .005 and, in Bass, the .01 in series with 470K),
then a 2x2 linear solve for node N and the output per sample (no iteration); the transistor resistance only changes `Rv`.
Output rails softly at +-4.2 V. Bass is levelled 7.2 dB up (internal makeup) so Norm and Bass match at the same settings.

## Measured behaviour (tests: `EnvelopeAutoWahProcessorTests`)

| Model | Rest peak | Top peak | Notes |
|---|---|---|---|
| MXR | ~350 Hz, -6.6 dB | ~3.0 kHz, +12.4 dB | duty 0.08 .. 0.87 |
| MXR Bass | ~90 Hz | ~970 Hz | caps x3.3 |
| Doctor Q Norm | ~200 Hz, +14 dB | ~4.5 kHz | Rv 2M .. 120 |
| Doctor Q Bass | ~200 Hz, +6 dB | ~4.5 kHz, +14 dB | lows flat at 0 dB |

Soft note vs hard note (220 Hz, 4 mV vs 200 mV): tuning 0.07 -> 1.00 (MXR) and 0.00 -> 1.00 (Doctor Q). Threshold 0.2 vs
0.9 on the MXR: 0.88 vs 0.08 at 20 mV.

CPU: linear, no oversampling, one ~10-multiply state update per sample plus the detector -- well under the 5 % budget
(far below the Mu-Tron, which oversamples).

Unity trims (registry, from `PedalUnityLevelTests`): MXR +16.13 dB, MXR Bass +3.22 dB, Doctor Q -8.78 dB. Pickup loading
impedances: 200K (MXR, R-7), 32K (Doctor Q, 47K in parallel with the Sensitivity pot's 100K).

## Naming

Display names follow the "-Style" convention: "MXR Envelope Filter-Style Auto-Wah", "MXR Bass Envelope Filter-Style
Auto-Wah", "Doctor Q-Style Auto-Wah". Registry keys: `MXREnvelopeFilterStyleAutoWah`, `MXRBassEnvelopeFilterStyleAutoWah`,
`DoctorQStyleAutoWah`.

## Not modelled

The DOD 440 (LED/LDR-tuned op-amp stage) and the EHX Y-Triggered Filter were looked at but their drawings are not legible
enough to model without inventing values; they are left out rather than guessed.
