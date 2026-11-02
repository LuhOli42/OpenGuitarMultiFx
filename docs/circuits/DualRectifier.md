# Dual Rectifier-Style Amplifier

A component-level model of the original **Mesa/Boogie Dual Rectifier 2-channel solo head**.

Display name: **Dual Rectifier-Style Amplifier** (trademark-safe convention).

## Sources

- Factory schematic set, six sheets dated 6-93 GEO. M.: preamp "RF-1F", power amp, FX loop, power supplies, and the
  two-part LDR switching matrix with its truth table.
  - URL: https://el34world.com/charts/Schematics/files/Mesa_boogie/Boogie_dualrectifier.pdf
  - Local copy and reading notes: `~/schematics-audit/drec/` (`NOTES.md`).
- Mesa's own 2-channel owner's manual (`2chRecto.pdf`): the panel, the rear switches and what each mode is.

Rebuilt 2026-11-02 after the user reported a very quiet clean channel and channels that were laid out wrong. The
previous model had invented Clean / Orange / Red by skipping V2A / V2B, and used values the drawing does not have
(220K V2A plate, 22µF cathode bypasses, a series 22K in the stack, a rheostat master).

## The real amp: two channels, five modes

The panel has two channels, **Orange** and **Red**. Each has its own Gain, Treble, Mid, Bass, Presence and Master.

The rear panel voices the channels with three switches:

| Switch | Positions |
|---|---|
| Orange Channel Gain | **Clean** / **Variable Hi Gain** ("Normal") |
| Channel Style Select | **Normal** (Orange = Vintage, Red = Modern) / **Orange to Modern** / **Red to Vintage** |
| Bias switch | **6L6** / **EL34** |

The rear panel also has **Rectifier Select** (5U4 tubes / silicon diodes) and **Power** (Spongy / Bold).

All five modes run through one tube chain. The LDRs re-voice it. The switching-matrix sheet prints the truth table
(ON = lit, ~500 Ω; OFF = dark, ~50 MΩ):

| LDR | Function | OR Normal | OR Clean | OR Modern | RD Modern | RD Vintage |
|---|---|---|---|---|---|---|
| 1 | Red input select | off | off | off | ON | ON |
| 2 | Orange input select | ON | ON | ON | off | off |
| 3a / 3b | V2A / V1A cathode cap | ON | off | ON | ON | ON |
| 4 | V1A output pad | ON | off | ON | ON | ON |
| 5 | Red gain pot | off | off | off | ON | ON |
| 6 | Orange gain pot | ON | ON | ON | off | off |
| 7 | Treble rolloff | ON | ON | off | off | ON |
| 8 | Red tone caps | off | off | off | ON | ON |
| 9 | Orange tone caps | ON | ON | ON | off | off |
| 10 | V3A cathode cap | ON | off | ON | ON | ON |
| 14 | Red master | off | off | off | ON | ON |
| 15 | Orange master | ON | ON | ON | off | off |
| 16 | Orange master bypass | off | ON | off | off | off |
| 19 | Feedback | ON | ON | off | off | ON |
| 20 | More feedback | off | ON | off | off | off |

Read as voicing:

- **Modern** = no global negative feedback and no treble rolloff.
- **Clean** = the cathode bypasses open, V1A's output padded through 2.2M ∥ 82p alone, the Orange master's 82K bypass
  leg in, and twice the feedback.
- **Vintage** = everything bypassed, with feedback and the treble rolloff.

`updateLdrs()` builds this table from the buses the sheet draws, not as a lookup:

- Orange bus: 2, 6, 9, 15, plus 3, 4, 10 unless relay RY1 (Clean) lifts them; Clean adds 16 and 20.
- Red bus: 1, 3, 4, 5, 8, 10, 14.
- 7 and 19 ride on Orange unless it is cloned to Modern, and join Red when it is cloned to Vintage.

Each cell fades in the log domain over 20 ms, like the real cells, so a mode change never steps the netlist.

## Preamp (sheet "RF-1F")

- **V1A**:
  - the input jack drives the grid directly (R211 1M leak); R221 220K plate from E;
  - cathode R291 1.8K, with C31 1µF in series with R271 47K; LDR3b shorts the 47K.
- **V1A output**: C21 .02 → R322 2.2M ∥ C10 82p, with R321 2.2M to ground. R110 680K ∥ C9 .002 joins through LDR4
  (the "pad").
- **Gain pots**:
  - LDR1 / LDR2 feed the **Red** / **Orange** Gain (1MA each, .001 top-to-wiper);
  - LDR5 / LDR6 take their wipers → R241 470K → V2A (C12 20p).
- **V2A**: R201 100K plate; R292 1.8K + C32 1µF / R272 47K (LDR3a).
- **V2B**: C22 .02 → R242 470K, R212 1M; R202 100K ∥ C6 .001 plate; R103 39K unbypassed (384 V / 6 V).
- **V3A**: C27 .02 → R106 330K / R225 220K; R224 220K plate; R293 1.8K + C33 1µF / R277 47K (LDR10).
- **V3B**: cathode follower (R203 100K) driving both stacks.

## Tone stacks and masters

V3B feeds the two stacks through LDR8 (Red) and LDR9 (Orange), with R215 1M between their inputs.

Both stacks share the same TMB core: 47K slope, Treble 250K, Bass 1M, Mid 25K (its wiper on the second .02), and a
1M Master.

| | Red | Orange |
|---|---|---|
| Treble cap | C4 680p | C5 500p |
| Extra network | Presence: R254 22K + C8 .003 + a 25K rheostat shunting the treble wiper | R263 82K ∥ C10 82p to LDR16 (master bypass); R2E3 22K + C7 .003 + R354 10K to ground, LDR7 shorting the 10K (treble rolloff) |
| Master to PI bus | LDR14 | LDR15 |

The bus carries a 10M resistor to ground. It is a numerical DC reference only.

## Power amp

- **PI**: 12AX7 LTP; 82K / 90K plates, each ∥ 120p; 75p plate to plate; .047 couplings; 220K leaks; 1.5K stoppers.
- **Output**: four 6L6GC or EL34 as two push-pull pairs; screens 1K / 2W. The bias supply is **−51 V (6L6) / −39 V
  (EL34)**, as the BIAS switch on the supply sheet sets it.
- **NFB**: FB → C51 .1 → R276 47K, with a second 47K joining through LDR20, all through LDR19.
  - Tapped at the model's 16 Ω secondary, so 4× the drawn resistance + R353 10K (47K → 198K).
  - The **Orange Presence** (25K + C52 .1) shunts this loop. The Red Presence is in its preamp stack. That is why the
    manual says a Red channel cloned to Vintage has both Presence controls active.
- **EL34 fit**: the codebase's plexi-region fit (Super Lead) with pair scaling. The Bias Switch swaps the netlist's
  pentode model live with `NodalCircuit::setPentodeModel()`. That is a copy of a model prepared off the audio thread,
  which only copies table pointers.

## Power supply

- **Rails** (sheet): A 460 V (plates), B 454 V (screens), C 422 V (PI, V3), D 406 V (V2), E 402 V (V1A).
- **Chain**: choke A → B, R602 2.7K, R252 22K, R261 15K, C61–C64 30µF.
- **Rectifier: Tubes** (two 5U4): 5× the diode bridge's series resistance, 1.7× sag depth, ~6% lower B+.
- **Power: Spongy**: the lower primary tap, every rail −10%. ESTIMATED: the sheet names the taps, not their voltages.

## Controls

**Page 1**, the real panel:

- Channel (Orange / Red), then that channel's own Gain, Treble, Mid, Bass, Presence and Master (`ChannelKnobMemory`).
- The rear switches: Orange Gain, Channel Style, Rectifier, Power, Bias Switch.

The visible knobs move only the selected channel's pots. The Orange Presence value is kept while Red is selected,
because it still acts in Red to Vintage (`ChannelKnobMemory::slotValue`).

**Page 2**, synthetic: Power Drive, Bias, Tube Feel, Speaker (4 / 8 / 16 Ω), Output, Cab.

**Old presets** (Channel = Clean / Orange / Red) migrate as follows:

| Old channel | New setting |
|---|---|
| Clean | Orange + Clean |
| Orange | Orange + Hi Gain |
| Red | Red |

The per-channel knob rows follow the same mapping.

## reducedOrder

The tone stacks and the whole preamp are netlists in both paths. Every preamp-side LDR, the Red Presence and both
stacks are therefore identical in the full and reduced models.

The behavioural power stage was refitted to the rebuilt full netlist; the old PI had V5A cut off, see below.

- **Modern** (the default, no loop) is the base:
  - `bmGain0` 55 comes from the small-signal level (Red, Gain .05, Master .25). It lands within ~2 dB at 100 Hz,
    1 kHz and 4 kHz.
  - The top section, bump and trim come from the AmpFullVsReduced chord search: worst band 6.4 → 1.9 dB, level
    under 1.2 dB.
- **The loop and the tube type** become dB of drive into the stage:
  - Vintage −14.5 dB and Clean −18.5 dB. These follow the full netlist's 10–16 dB small-signal drop.
  - EL34 +3 dB.
  - They are interpolated in `nfbAmount`: 0 for Modern, 1 for Vintage, ~2 for Clean.
  - The saturated levels then match the full model within 1.6 dB with no output correction.
- **The Orange Presence shelf** scales with the loop, so it is inactive in Modern.

AmpFullVsReduced in every mode:

| Mode | Level | Worst band |
|---|---|---|
| Red Modern | 1.20 dB | 1.94 dB |
| Red Vintage | 1.10 dB | 3.32 dB |
| Orange Normal | 1.17 dB | 4.06 dB |
| Orange Clean | 1.57 dB | 4.98 dB |
| Red Modern EL34 | 1.47 dB | 2.23 dB |

Bounds: 4 dB level, 5 dB band.

CPU in the reduced path is 12.2% (the DualRectifier CPU test), up from ~10%. The cost is the second tone stack and
the LDR network.

## The old PI was half dead

The previous netlist returned V5A's grid through 1M to the feedback node. That node sits near 0 V, while the
cathodes sat at ~30 V, so V5A was cut off:

- the phase inverter ran single-ended;
- the global feedback and the Presence did nothing (the old doc read this as "measures 0.0 dB at every setting");
- the power amp splattered asymmetrically.

The drawing biases both grids from the tail node T. `AmpReferenceHealthTests` now checks this for every amp (see
CircuitFamilies.md); six more amps carry the same copy-pasted PI.

## Level and calibration

- **Per-mode level match**: an eased output trim, `modeLevelMatchDb`, in dB against Red Modern. It is the E3 / DI
  midpoint from `AmpSwitchLevelSurvey` (`SWITCH_BASE=drec_channel=0` reaches the Orange modes):

  | Orange Normal | Orange Clean | Orange to Modern | Red to Vintage |
  |---|---|---|---|
  | +13.6 dB | +11.3 dB | +0.2 dB | +13.3 dB |

  After it, every mode sits within ±1.7 dB of Red Modern. The exception is Clean, which is ±5 dB around 0: a clean
  mode and a saturated one compress differently between the hot tone and the DI.
- **Registry trim**: −18.23 → −16.13 dB, which brings PedalUnityLevel back to 0.0 dB.
- **EL34 idle**: Koren's EL34 set with mu re-placed from 11 to 12.4 idles at ~49 mA a tube on the Bias Switch's −39 V.
  Koren's mu 11 gives ~84 mA there, and the Super Lead plexi fit ~220 mA. The 6L6s idle at ~45 mA a tube.
