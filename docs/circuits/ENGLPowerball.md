# ENGL Powerball-Style Amplifier

A component-level model of the **ENGL Powerball (E645)**, a four-channel 100 W head.

Display name: **Powerball-Style Amplifier** (trademark-safe convention).

## Sources

- Factory drawing 645-1, two sheets: the audio path and supplies on sheet 1, switching logic and the tube monitor on
  sheet 2. https://el34world.com/charts/Schematics/Files/Engl/Engl_powerball.pdf
- The E645 operator's manual, for the panel and what each switch does.
- Local copies and reading notes: `~/schematics-audit/engl/` (`NOTES.md`). Render at 900 dpi to read the values.

Rebuilt 2026-11-02. The user reported that the channel split did not express the amp: each channel felt like a boost
pedal on one channel. The old model was one preamp with synthetic per-channel "tap gains"
(`bmTapGain` 400 / 36000, `chanSens`), a single tone stack in the wrong place (before V6B) and a phase inverter with
one half cut off.

## The real amp

The two main channels each have two modes, selected by the Ch I/II and Gain Lo/Hi buttons: Clean / Crunch and Lo Lead
/ Hi Lead. The panel shares its controls like this:

- **Gain, Bass and Middle**: one per main channel.
- **Treble**: one each for Clean, Crunch and Ch II.
- **Volume**: one per mode.
- **Ch II's two Middles**: Open and Focused, chosen by the Style button. Style also changes Ch I's caps.
- **Bright and Bottom on Ch I**; a separate Bottom for each Lead mode.
- **Global**: Presence, Depth Punch, Master A / B.

## Signal path (sheet 1)

1. **V5A** (R3 100K, R2 1K + CE1 22µ). The input jack goes straight to the grid.
2. **C1 47n → N1** (R4 2.2M). From there:
   - **Ch I**: C2 15n (shorted by **Bottom**) → R5 100K → **Ch I Gain** P1 1MA. C3 100p (**Bright**) runs from R5's top
     to the wiper. LDR LD1 feeds V5B.
   - **Ch II**: R6 100K → **Ch II Gain** P10 1MA, then LD2.
3. **V5B** (R9 100K, R8 1.5K + CE2 22µ) → C4 47n → N4 (R10 2.2M) with four dividers. Relay RE3 (Gain) picks:
   - **Ch I**: R15 1M / R17 220K (×0.18, Clean) or R18 680K / R16 330K (×0.33, Crunch). RE1A sends this straight to
     V6B.
   - **Ch II**: R11 220K / R12 470K (×0.68, Lo) or R13 100K / R14 470K (×0.82, Hi) into **V6A** (R20 330K,
     R19 3.3K + CE4 1µ), then C5 47n, then RE1A. RE1B grounds V6A's output on Ch I.
4. **R21 220K / R22 1M → V6B** (R24 100K, R23 1.5K + CE5 22µ) → C6 100n (R25 1M) → RE2B → a tone stack.
5. **Ch I stack**: C8 470p (+ C7 via RE6A) → Clean Treble P4 / Crunch Treble P5. RE5 picks whose bottom returns to
   the bass leg and whose wiper is heard. Then R27 100K slope, C9 220n, Bass P2 1MA, Middle P3 20KB, and C10 100n
   (+ C11 47n via RE6B). The selected treble wiper feeds both Volume legs: R28 220K → Clean Volume P6 250KB, and
   R29 330K → Crunch Volume P7 250KA. RE4A picks the wiper.
6. **Ch II stack**: C13 470p (+ C12 via RE7A) → Lead Treble P14, R31 100K slope, C14 220n, Lead Bass P11 1MA. RE8
   picks Open Middle P12 (C16 22n) or Focused Middle P13 (C15 47n). The treble wiper feeds R33 470K → Lo Lead Volume
   P15 and R35 470K → Hi Lead Volume P16. **Lo / Hi Lead Bottom** add a path from the bass node through R32 330K and
   R34 / R36 680K. RE4B picks the wiper.
7. **RE2A → R40 220K / R41 100K → V7A**, the FX send: a cathode stage with R43 100K ∥ C17 10n on the plate, a 1K
   unbypassed cathode, and CE6 4.7µ out of the cathode into R44 47K.
8. **The FX loop's jacks are not modelled.** That is the rule for every amp. What is modelled is the dry path the
   unplugged jacks leave: R45 10K + half of the Balance pot (25K) → **V7B** (R47 100K; R49 100K, R48 1.5K + CE7
   22µ) → C18 100n → R50 / R51 100K → **Master A / B** (1MB each). RE9 picks the master → C19 100n → PI.
9. **PI V8**:
   - Both grids are biased from the tail node T: R53 100K to V8A, R59 150K to V8B.
   - The cathodes go through R55 470R to T, then R56 22K to B, then R57 4.7K to ground.
   - C24 100n AC-grounds V8B's grid to B.
   - The feedback lands on B through R58 1.5K.
   - Plates: R61 82K and R62 100K.
10. **Feedback network**: from the 8 Ω tap, through **Presence** P8 50KC (its wiper drives R58; its far end returns
    through C25 100n) and **Depth** P9 1MA → C27 10n → R60 22K.
11. **Output stage**: four 6L6GC as two pairs (330K leaks, 2.2K stoppers, 470R screens), bias −55 V supply. The model
    idles at ~28 mA a tube; the drawing's note says 28–34 mA.

## Model structure

NodalCircuit allows 48 unknowns per block, so the chain splits in three, each where the split is exact or very
close:

| Block | Contents | Joins the next block as |
|---|---|---|
| `pre` | V5A … V6B, ~28 nodes | V6B's unloaded plate voltage behind R24 ∥ ra (38.5K): its Thévenin output |
| `tone` | Linear, no tubes: C6, both stacks, the Volumes, R40 / R41 | An ideal voltage at V7A's grid, which is a high-impedance input |
| `post` | V7A, V7B, the Masters | — |
| `power` | PI + output stage (full model only) | — |

Every relay contact and pushbutton is a resistor that fades in the log domain: 1 Ω closed, 100M open, over 5 ms.
LD1 / LD2 fade 1K ↔ 50M over 20 ms. The hidden modes' Bottom buttons keep their last state.

**The preamp rail is fixed at prepare().** Fed live from the supply, the power stage's current reached V5A through
the single lumped preamp node and motorboated at ~28 Hz. The real amp's chain of RC decoupling stages (R26 4.7K +
CE3, R138 + CE10, …) does not have that loop.

## Controls

Page 1 = the real panel:

- **Channel**: Clean / Crunch / Lo Lead / Hi Lead, i.e. the Ch I/II and Gain Lo/Hi buttons together.
- **Knobs**: Gain, Bass, Middle, Middle Focused (Ch II only), Treble, Volume. Each is shared the way the panel shares
  it (`ChannelKnobMemory`).
- **Switches and globals**: Style (Open / Focused), Bright (Ch I), Bottom (the active mode's), Presence, Depth Punch,
  Master A, Master B, Master A/B.

Page 2 (synthetic): Power Drive (scales the Master's output into the power section, unity at noon), Bias, Tube Feel,
Speaker, Output, Cab.

Presets saved before the rebuild migrate:

| Control | Old preset | New layout |
|---|---|---|
| Gain | one per channel (4) | one per main channel; Ch II takes the old Lead value |
| Treble | one per channel pair (2) | Clean / Crunch / Lead, each from the old pair value |

## Calibration

- **reducedOrder**: the behavioural stage keeps its old shape constants. `bmGain0` is 20 and `bmLevelTrim` 0.40.
  - AmpFullVsReduced at the defaults: level 0.28 dB, band 1.47 dB (bounds 1.0 / 2.5).

    | Mode | Level | Band |
    |---|---|---|
    | Clean | 0.70 dB | 1.26 dB |
    | Lo Lead | 0.37 dB | 1.43 dB |
    | Focused | 0.17 dB | 1.22 dB |
    | Crunch | 2.5 dB | 0.95 dB |

  - Crunch is the least saturated mode. A lower G0 fixes its level but breaks the band shape everywhere. Its level is
    absorbed by the mode trim, which is measured on the reduced path the app runs.
- **Per-mode output level match** against Hi Lead (E3 / DI midpoint): Clean +4.7 dB, Crunch +11.0 dB, Lo Lead 0. The
  Crunch Volume's log taper puts it ~10 dB under its peers at noon.
- **Registry trim** −14.24 → −15.65 dB; PedalUnityLevel reads 0.00 dB.
- **CPU** (reducedOrder): 16.4%, against ~12% for the Dual Rectifier, because the six triodes are solved every sample.
  The LinearPortNetwork / CompiledCircuit pattern would be the next step if that matters.
