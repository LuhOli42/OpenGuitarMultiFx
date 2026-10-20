# Overdrive / distortion circuit-fidelity audit (2026-10-19)

Same 3-layer method as the amp audit (PRs #99-#111): **topology** (netlist vs published schematic), **controls**
(every real control present and wired, full-order and compiled/reduced paths), **behaviour** (RenderFx renders of
`Tests/fixtures/di/guitar-di.wav` -- `bass-di.wav` for the bass pedals -- and `probe-sine220.wav`, `--quality=high`).
Only verifiably wrong things were changed. PedalUnityLevel trims were not touched.

Scope = the families named for this batch (TubeScreamer, RAT, Klon, ODR-1, Guv'nor, Shredmaster, Tube Driver,
Darkglass), restricted to models registered in `EffectRegistry.cpp`: TS808/TS9/TS10/TS9B, RAT/RAT 2/Turbo RAT,
Centaur, ODR-1, Guv'nor, Tube Driver, Darkglass B7K. **No Shredmaster is registered**, so none was audited (or invented).

**Not covered here** (registered OD/dist/fuzz models outside those families, left for a later batch): BD-2, Blues
Breaker, Crunch Box, DOD 250, DS-1, DT-1, Distortion+, HM-2, Metal Zone, OCD, OD-1, ODB-3, Overdriver, SansAmp BDDI,
Zendrive, and the fuzzes (Big Muff family, Fuzz Face family, Tone Bender family, Green Ringer, Octavia, Super Fuzz,
Tone Machine).

## Findings

| Pedal | Topology | Controls | Behaviour | Action |
|---|---|---|---|---|
| TS808 / TS9 / TS10 | Matches Geofex + barbarach redraw: buffer, non-inv op-amp with antiparallel Si in feedback, active tone, output follower. TS808/TS9 differ only in R14/R15 (100/10k vs 470/100k), TS10 adds its 3 documented changes. | Drive, Tone, Level -- all real controls. | TS808 vs TS9: identical to 0.0 dB / same THD; TS10 within 0.2 dB. | None. Convergence is **real**: Geofex says they are the same board apart from the op-amp and two output resistors, which only matter into a low-impedance load (see TubeScreamerStyleOverdrive.md). |
| TS9B | TS9 clipper + Bass/Treble tone + dry Mix summer + Level (Ibanez TS9B). | Drive, Mix, Bass, Treble, Level -- complete. | Distinct from TS9 (8.5 dB band signature, lower THD with the dry leg). | None. |
| RAT / RAT 2 / Turbo RAT | Single op-amp (LM308; OP07 Turbo) hard clip to Si diodes (LEDs Turbo) + Filter + Volume, compiled pipeline. | Distortion, Filter, Volume -- complete. | Distortion 0 = clean (THD 0.14 %), up to ~40 % THD; Filter 0->1 = -26 dB at 8 kHz+. RAT vs RAT 2 2.6 dB, Turbo vs RAT 9 dB apart. | None. Rev-P vs RAT 2 values already documented in RatStyleDistortion.md. |
| **Centaur (Klon)** | Ge pair, dual-gang gain, clean/dirty sum, treble, all present. **Supply rails wrong**: both op-amps on +-7.5 V around 4.5 V. ElectroSmash: IC1 (buffer + gain) on 9 V, IC2 (sum + treble) on the +18/-9 V charge pump. | Gain (dual gang), Treble, Output -- complete. | Gain 0 clean (THD 0 %) -> 24 % at full; treble 0->1 +24 dB at 8 kHz+. | **Fixed rails** (gain stage 1.5..7.5 V, IC2 -7.5..16.5 V). With the DI and probe the stage stays inside both old and new rails except at full Gain (max sample diff 0.0011 on a 0.25 peak, -47 dB): the fix is a correctness fix whose audible effect only appears with hotter pickups. |
| ODR-1 | Matches Nobels rev 5 drawing: U1A gain with **symmetric** 1N4148 pairs (the task's "asymmetric" premise is not what the drawing shows), Spectrum network, U2. | Drive, Spectrum, Level -- complete. | Drive 0 clean (0.6 %) -> 21 %; Spectrum moves the whole band balance. | None. |
| Guv'nor | TL072 gain + red-LED clipping + passive 3-band (ElectroSmash). Op-amp, **not JFET**, per the published analysis. | Gain, Bass, Middle, Treble, Level -- complete. | Gain 0 clean -> 31 % THD; Treble 0->1 +18 dB top band. | None. |
| Tube Driver | GGG redraw rev 2003-12-09: IC1a buffer, IC1b inverting "Tube Drive" stage (R5 500K rheostat feedback), two 12AX7, Hi/Lo, Level. | Tube Drive, Hi, Lo, Level -- complete. | Tube Drive 0 = **-78 dB** (inverting stage, rheostat feedback at 0 ohm = gain 0, like the real pedal), then 14 % -> 31 % THD with even harmonics (h2 0.24 at full: the only pedal here with tube-like 2nd harmonic). | None (silence at Tube Drive 0 follows from the schematic). |
| **Darkglass B7K** | Buffer, Grunt/Attack, drive op-amp, 4049 CMOS clip + Si shunt, Blend, 4-band EQ, Level. **Hi Mid band at 3.30 kHz** vs the published 2.8 kHz (the code comment claimed ~1.2 kHz -- wrong). | Blend, Level, Drive, Bass, Lo Mid, Hi Mid, Treble, Grunt, Attack -- complete for the original B7K (the Ultra's mid-frequency switches belong to a different pedal). | EQ peaks (measured): Bass 84 Hz, Lo Mid 963 Hz, Hi Mid 3304 -> **2863 Hz**, Treble shelf +6 dB @ 5 kHz. | **Fixed Hi Mid** (gyrator C 2.2 nF -> 3.0 nF), docs + comment corrected. |

## Open (measured, not changed -- no schematic to verify against)

- **B7K Drive range is nearly inert**: Blend full wet, bass DI, Drive 0 -> 1 moves RMS by +0.4 dB, crest 5.6 -> 5.2 dB, HF
  (> 2 kHz) energy +2.9 dB; THD on the 220 Hz probe 25.2 -> 25.4 %. Drive stage min gain 4.7x into the -22x CMOS stage
  means it is saturated already at Drive 0. The doc says "~6x..100x", the code gives 4.7x..47x. The real B7K's
  Drive pot range is not published (clone traces are of the Ultra); fixing it needs a trace of the original.
- **Centaur Gain taper**: 0.75 -> 1.0 adds +13.7 dB (audio taper on the dual-gang 100K whose taper is taken from
  clone BOMs). Real Klons are known for a steep top end; not changed without the original pot's taper.

## Measurement method

`measure.py`-style sweep: Drive {0, .25, .5, .75, 1} on the DI (RMS re DI, 7-band spectrum re DI) and the 220 Hz
probe (THD, h2, h3), Tone {0, .5, 1}, then every pair's level-removed band signature. Pipeline pedals (RAT x3,
ODR-1, B7K) are measured on the engine the registry uses (compiled pipeline).
