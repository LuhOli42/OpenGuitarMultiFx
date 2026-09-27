# Gain audit: every pedal against the real one (2026-09-20)

Measured on the processors with no unity trim, small signal (0.2 mV at 1 kHz) at the stage
outputs and overall with Level max, plus 0.3 V peak hot-input outputs; compared with published
figures. "Real" numbers are quoted from the source named; where a source gave none the row says
so instead of guessing.

| Pedal | Real (source) | This model | Verdict |
|---|---|---|---|
| **DS-1** | Transistor booster 35 dB; op-amp stage 1 + VR/4.7K = 0 to 26.5 dB with the 100K pot; tone stack -12 dB (-20 dB notch at 500 Hz); clipper +-0.7 V (1.4 Vpp), 350 mVpp after the tone stack ([ElectroSmash](https://electrosmash.mas-effects.com/boss-ds1-analysis.html)) | Booster 34.4 dB. Op-amp stage 0 / 16.0 / 21.3 / 24.6 / 26.9 dB at knob 0 / .25 / .5 / .75 / 1 (formula: 0 / 16.0 / 21.3 / 24.6 / 26.9). Hot input, Level max: +-0.245 V (0.49 Vpp) | Gain **matches the formula exactly**. Output ~3 dB above the quoted 350 mVpp (test frequency of that figure unknown) |
| **Tube Screamer 808/9/10** | 1 + (51K + Drive)/Zleg, Zleg = 4.7K + 0.047 uF: 21.6 dB (Drive min) to 41.4 dB (max) at high frequency; hump at 720 Hz ([ElectroSmash](https://electrosmash.mas-effects.com/tube-screamer-analysis.html)) | Op-amp out at 1 kHz: 19.7 / 23.5 / 29.9 / 35.0 / 39.0 dB (formula at 1 kHz: 19.8 min, 39.6 max). Overall hump 0.5-1 kHz | **Matches** |
| **Klon Centaur** | Gain stage max 40 dB (100x) around 1 kHz; treble stage +18.24 / -8 dB (Gv max 8.16, min 0.4); Ge diodes 0.35 V ([ElectroSmash](https://electrosmash.mas-effects.com/klon-centaur-analysis.html)) | Gain stage alone by formula 40.05 dB; in the circuit 36.0 dB at 1 kHz (the input network loads it by 4 dB; the quoted 40 dB is the stage). Treble stage formula values 8.17 / 0.398 = the published ones | **Matches** (the 4 dB is the documented loading). Level/Gain interplay is the pedal's dual-gang design |
| **BD-2** | Second stage "max gain just under 40 dB, flat 100 Hz - 6 kHz" (PedalPCB breadboard thread); tone at noon: 3 dB down at 100 Hz; clean and nearly transparent at 25% gain | Stage 1 at max: 40.3 dB at 2 kHz (23.4 dB at min); overall response flat within 5 dB from 100 Hz to 8 kHz at max gain | Consistent. No published per-stage tone numbers found to compare further |
| **Rangemaster-style booster** | Gv = gm Rc = 0.008 x 10K = 80 = 38 dB, high-pass 2.6 kHz ([ElectroSmash](https://electrosmash.mas-effects.com/dallas-rangemaster.html)) | 34.8 dB at 8 kHz at max, 29.7 dB at 1 kHz, corner ~2-3 kHz | 3 dB below; the transistor's bias current (hence gm) is an assumption in both |
| **OD-1** | Non-inverting stage 1 + (33K + Drive 1M)/Zleg, Zleg = R6 4.7K + C3 0.047 uF: 16.5 dB (min) to ~45 dB (max) at 1 kHz (same family as the SD-1's 47 dB at high frequency); Drive is 1MA per Aion FX's Corona parts list | **Was wrong**: wired as an inverting stage (0 dB to 30 dB). Fixed: 15.8 / 24.3 / 33.4 / 39.4 / 43.8 dB | **Fixed today**, see OD1StyleOverdrive.md |
| **HM-2** | No published gain figure found. Documented: the DIST pot does nothing from 9 to 3 o'clock; Ge diodes are a 0.3 V coring gate; Low / High gyrators at ~87 Hz and ~960 / 1280 Hz | Q6+Q7 44.5 dB; IC1B adds 14.7 dB (1 + 220K/47K with DIST max); DIST flat from 0.25 to 0.75 knob; Ge gate present; Low +-19 dB at 90 Hz, High +-21.5 dB at 1 kHz | Structure and behaviour agree with the documented ones; the absolute gain and the EQ range are **not verified** against a source |
| **Distortion+** | Gain 1 + 1M/(4.7K + RV1): max 213 (46.5 dB), min quoted 1.5 (3.5 dB; its own formula gives 1.995); 741 GBW 1 MHz; clip 700-800 mVpp; C3 corner 720 Hz at max gain ([ElectroSmash](https://electrosmash.mas-effects.com/mxr-distortion-plus-analysis.html)) | Matches the closed form (input filters, 741 pole, C3 leg) within 0.1 dB at 100 Hz - 1 kHz for 4 Distortion settings; max gain at 1 kHz 187x (45.4 dB); clip +-0.367 V = 0.73 Vpp (Is fitted to this) | **Matches** except the published min gain (arithmetic used) and the clip level (fitted, not independent) |
| **DOD 250** | Same circuit as the Distortion+ with 500K reverse-log Gain, 25 pF across the 1M, 1N4148 pair, 100K Level (General Guitar Gadgets schematic); no published gain figures found | Matches its closed form within 0.1 dB at 100 Hz - 1 kHz (2.9 / 8.4 / 39.5 / 177x at 1 kHz for knob 0 / .5 / .8 / 1); clip +-0.53 V | Consistent with the schematic; nothing published to compare the sound with |
| **Guv'nor** | Stage 1 max 46.45x (33.3 dB); stage 2 -68x (36 dB) per ElectroSmash's text; clip 1.8-2 V with red LEDs; C3/R3 corner 723 Hz; C2 low-pass 13.2 kHz at max | Stage 1 1.0 / 19.2 / 37.6x at 1 kHz (formula-exact incl. TL072 and C2), max 46x at high frequency; **stage 2 is 6.2x at minimum Gain, 68x at maximum, not a fixed 68x**: the drawing feeds it through the rest of the Gain pot; LED clip 1.797 V | Stage 1 **matches**; stage 2 differs from the text and follows the drawing; the tone stack is not verified (graphs only) |
| **Blues Breaker** | No published gain figures found (the 1992 schematic; Aion FX describes a lower-drive first version with 27K/33K and a later 3k3/4k7) | Stage 1 1.0 / 2.8 / 4.8x at 1 kHz, stage 2 2.0-21x (formula-exact); stage-2 swing limited to ~1 V by the diodes; total up to ~30 dB | Consistent with "a subtle pedal, warm at low drive, a little fuzzy at high gain"; nothing quantitative to compare |
| **RAT** | Gain up to 1 + 150K/47 ohm-ish ~ 3600 (71 dB); op-amp GBW ~1 MHz with the 30 pF (LM308 datasheet); filter 475 Hz - 32 kHz (community docs, not re-checked) | Stage gain formula-exact within 0.25 dB (474x at 100 Hz, 1237x (62 dB) at 1 kHz, 230x at 4 kHz at the maximum); filter 5 kHz/200 Hz ratio 4.06 -> 0.45; diode clip 0.62 V | Matches the schematic; **slew rate not modelled** (documented) |

Output level (the maximum a pedal can deliver) has a published figure only for the DS-1; the
others are not verified.

## Large signal (knob sweep with guitar-level input)
THD and fundamental output at 220 Hz, Level max, tone noon, input 10 / 30 / 100 / 300 mV peak,
knob 0 / .25 / .5 / .75 / 1. Checked against what is published or documented:

| Pedal | What the sweep shows | Published / documented |
|---|---|---|
| TS808 | Knob 0: 0.09% THD at 30 mV, 3.4% at 100 mV, 12.7% at 300 mV (onset ~100 mV); THD grows with the knob and the output compresses (300 mV in: 520 -> 556 mV out from knob 0 to 1) | "Unclipped for Vin < ~90 mV peak at minimum gain" (ElectroSmash) |
| BD-2 | Knob .25: 0.3% at 30 mV, 1.5% at 100 mV, 22% at 300 mV (clean, then breaks up when hit hard); noon 25% at 100 mV; above 2 o'clock the output stops growing (2.30 -> 2.37 V) | "Gain at 25%: clean, nearly transparent, a little compression, breaks up when picking harder; past 2 o'clock thinner/compressed without more saturation" |
| HM-2 | Knob .25, .5, .75 give the same THD and level (26-28% at 10 mV, output ~300 mV); only the ends differ | "The DIST control does nothing from 9 to 3 o'clock; only near the ends" |
| DS-1 | Output saturates at +-245 mV for every knob position >= .25 (THD 18-31%); even knob 0 is 13% at 100 mV (the booster alone is 35 dB) | Clipper fixes the output level; the booster is always in |
| OD-1 | Knob 0: clean to ~100 mV (16.5 dB stage); noon 17% at 100 mV; max 11% already at 10 mV | Same 16.5 dB minimum gain as the TS family |
| Klon | Knob 0-.75 almost clean up to 100 mV (0.03 / 0.4 / 2%); max: 6% at 30 mV, 18% at 100 mV; output at max Gain is 2.2 V, 7 dB above noon (the second gang trades clean for driven signal) | Gain knob blends clean and driven paths; noon is a light overdrive |
| Rangemaster | THD identical for every knob position (6.5% at 100 mV, 24% at 300 mV), only the level scales (knob^2): the pot is the collector load, so the exponential input nonlinearity is the same | The boost control is the volume; the distortion is input-driven |

Not checkable: no measured large-signal figures were found for the Klon, HM-2, DS-1 or OD-1
beyond the descriptions above.

## Bassman-Style Amplifier (2026-09-21)
Audited against Kuehnel's published stage gains, its open-loop transfer curve and its supply-sag response, at the
same operating conditions; table in [Bassman5F6A.md](./Bassman5F6A.md). Summary: V1 -30.1 (published -32.2), V2A
-21.4 (-20.7), phase inverter +2 dB (Koren 12AX7 gm), power stage third harmonic 5.5% at full swing (5%), sag -54 V
(-57 V), output 52-54 W (~45-50 W). The power tube's Koren parameters were **fitted** to the published curve
because the standard 6L6GC set gave 0.8% third harmonic where the amp has 5%: a gain audit on a stage's numbers alone
would have passed it (its gain at full drive was right); the shape of the transfer curve is what carries the
Bassman's character.
