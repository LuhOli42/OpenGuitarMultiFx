# Rivera Knucklehead Reverb (three-channel revision)

The Rivera Knucklehead Reverb is Paul Rivera's flagship 100 W head — the early K100 was "two amps in one", and the
later revision this model targets (the rev-3 schematic era, sold with the pull-pot front panel and a third,
Ninja-always-on lead channel — the "Tre" behaviour) adds Clean / Crunch / Lead with pull-switches on the pots:
a blackface-flavoured clean channel, a cascading Crunch, and a high-gain Lead, a real long-tailed-pair phase
inverter, four 6L6GCs on fixed bias, and Rivera's signature Presence + Focus controls in the global
negative-feedback loop.

## Sources

- The factory service schematic **"KNUCKLEHEAD REVERB"** (schematicheaven `rivera_knucklehead.pdf`, rev 3,
  11.18.93): 5× 12AX7A (V1–V5), 4× 6L6GC (V6–V9), relay-switched channels, test-point rail marks TP4 318 V,
  TP5 206 V, TP12 350 V, TP41 −47 V bias.

## What the real circuit is

- **CH1 (clean)**: V1A → Volume (VR101 1 MEG) → CH1's own TMB (VR104/VR103/VR102, 270 pF treble cap, 33 k slope,
  .047/.1 µF) → V4A recovery (22 k plate, 1k5 + 22 µF) → Master (VR105 1 MEG).
- **CH2 (crunch)**: V1B → Gain → CH2's own TMB (270 pF, 33 k slope, .047/.1 µF, 25 k mid) → V2A (1k5 + 22 µF, 100 k)
  → V2B (1k5 + 1 µF, 100 k) → Master (VR110 250 k).
- **CH3 (lead)**: the same path with the pull-Ninja's extra triode permanently in circuit — the "Tre" behaviour.
- **PI**: V5 long-tailed pair — 470 k grid leaks, 680 R cathode link, 8k8 tail leg, 82k5/100 k plates, .022 couplings,
  150 k grid leaks to the −47 V bias node, 1 k stoppers.
- **Power**: 4× 6L6GC as two paralleled pairs, cathodes to ground, screens ~445 V; global negative feedback from the
  speaker terminal through 47 k (R201) into the PI tail's feedback node — carrying **Presence** (VR112 25 k + C203
  .1 µF) and **Focus** (VR111 250 k, described as low-frequency damping).
- **Supply**: solid-state bridge → ~460 V plates → screens; TP marks as above.

## What is modelled

Three blocks, same structure as the Trainwreck Express model:

1. `pre` — **the three channel paths in one netlist** (the real amp's relays just route; all paths are always solved
   and the Channel control selects which output feeds the power section): CH1 = V1A (47 k plate, per the drawing) →
   Volume → TMB (50 k mid) → V4A (22 k plate) → 1 M Master. CH2 = V1B (100 k) → Gain → TMB (25 k mid) → V2A →
   V2B → 250 k Master. CH3 = the CH2 chain with the Ninja stage (V3B) forced on, matching the real amp's
   Ninja-always-in lead channel.
2. `power` — V5 LTP exactly as drawn → 4× 6L6GC (two pair-models) → ~1k8 centre-tapped OT → 16 Ω tap → speaker
   model. NFB from the speaker node through 47 k into the tail feedback node; Presence = variable R + .1 µF shunt
   on the feedback node (HF), Focus = VR111 + 2.2 µF + a behavioral 10 H inductor in series to ground. This forms
   a series-resonant LF shunt, with `f0 = 1/(2π√LC)` (about 33.9 Hz for these modeled values). The cap blocks DC.
   Knob up = more feedback removed = more LF in the output.
3. `supply` — SS bridge → 100 µF reservoir → 500 R screens → RC-filtered PI (~318 V) and preamp (~250 V) taps.

Tubes: Koren ECC83 for all 12AX7A sections; Koren 6L6GC pair set for the output pairs.

## Controls (page 1)

| Param | Real control |
|---|---|
| Channel | Clean / Crunch / Lead selector (CH1 / CH2 / CH3 — the amp's channel relays) |
| Volume | CH1 Volume |
| Gain | CH2/CH3 Gain |
| Treble / Middle / Bass | the tone controls (drives the selected channel's own stack) |
| Master | channel masters |
| Presence | Presence (HF negative-feedback lift) |
| Focus | Focus (LF negative-feedback lift) |
| Bright | CH1 Treble pull-Bright (C107-style 120 pF across the treble control) |
| Notch | CH1 Middle pull-Notch (mid-range notch shunt) |
| Contour | CH1 Bass pull-Contour |
| Scoop | CH2/CH3 Middle pull-Scoop |
| Boost | CH1 Master push-Boost (extra clean triode stage) |
| Ninja | CH2/CH3 Master pull-Ninja Boost — the LDR-switched V3B triode stage (always engaged on CH3) |

Page 2 (synthetic): Power Drive, Bias (the fixed-bias trimmer), Tube Feel, Speaker (4/8/16 Ω on the 16 Ω tap),
Output.

## Honest simplifications

- **Reverb is not modelled** — the real spring tank (V3A driver / recovery) is documented but out of scope; use the
  chain's own reverb block after the amp.
- CH3 is voiced as the Ninja-always-on variant of CH2 (TRE behaviour; the FS-7 routes Ninja to CH2 only).
- All channels' EQs are driven by one set of T/M/B params (the real amp has one tone row per channel; the knobs track).
- The pull-switch functions are modelled at their switch positions (bright cap, mid notch/scoop shunts, bass contour,
  the V3B ninja triode); the CH2 gain-pot PULL NOTCH (C105) is folded into Scoop's voicing rather than given a
  separate eighth switch.
- Focus's 2.2 µF capacitor and behavioral inductive element are a modeling choice, not a traced topology; the
  schematic area is ambiguous. A plain series-RC shunt cannot select LF: its capacitor opens at low frequency and
  increasingly shunts highs as frequency rises. The series inductor makes the modeled shunt resonant and LF-selective;
  the capacitor also blocks DC through the PI-tail feedback node.
- The NFB secondary winding sense follows the Twin Reverb model's convention; verified stable in the silence test.

## Verification targets

- Rails ≈ 460 / 445 V; PI plates ~230–260 V; bias ≈ −47 V; matched pair idle ~50–80 mA per pair.
- The NFB loop is stable at all settings (silence AC RMS < 0.01 on both channels).
- Lead channel out-gains the clean channel at matched settings; Presence lifts HF, Focus lifts LF.
- CH1 Boost and CH2 Ninja each add a stage's worth of gain (>1.5× RMS in the sweep test).
