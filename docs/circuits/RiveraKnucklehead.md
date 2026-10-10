# Rivera Knucklehead Reverb (K100, 1993)

The Rivera Knucklehead Reverb is Paul Rivera's flagship 100 W head — "two amps in one": a blackface-flavoured clean
channel and a cascading high-gain lead channel, a real long-tailed-pair phase inverter, four 6L6GCs on fixed bias,
and Rivera's signature Presence + Focus controls in the global negative-feedback loop.

## Sources

- The factory service schematic **"KNUCKLEHEAD REVERB"** (schematicheaven `rivera_knucklehead.pdf`, rev 3,
  11.18.93): 5× 12AX7A (V1–V5), 4× 6L6GC (V6–V9), relay-switched channels, test-point rail marks TP4 318 V,
  TP5 206 V, TP12 350 V, TP41 −47 V bias.

## What the real circuit is

- **CH1 (clean)**: V1A → Volume (VR101 1 MEG) → CH1's own TMB (VR104/VR103/VR102, 270 pF treble cap, 33 k slope,
  .047/.1 µF) → V4A recovery (22 k plate, 1k5 + 22 µF) → Master (VR105 1 MEG).
- **CH2 (lead)**: V1B → Gain → CH2's own TMB (270 pF, 33 k slope, .047/.1 µF, 25 k mid) → V2A (1k5 + 22 µF, 100 k)
  → V2B (1k5 + 1 µF, 100 k) → Master (VR110 250 k).
- **PI**: V5 long-tailed pair — 470 k grid leaks, 680 R cathode link, 8k8 tail leg, 82k5/100 k plates, .022 couplings,
  150 k grid leaks to the −47 V bias node, 1 k stoppers.
- **Power**: 4× 6L6GC as two paralleled pairs, cathodes to ground, screens ~445 V; global negative feedback from the
  speaker terminal through 47 k (R201) into the PI tail's feedback node — carrying **Presence** (VR112 25 k + C203
  .1 µF) and **Focus** (VR111 250 k + a larger cap, the low-frequency counterpart).
- **Supply**: solid-state bridge → ~460 V plates → screens; TP marks as above.

## What is modelled

Three blocks, same structure as the Trainwreck Express model:

1. `pre` — **both channel paths in one netlist** (the real amp's relays just route; both paths are always solved and
   the Channel control selects which output feeds the power section): CH1 = V1A (47 k plate, per the drawing) →
   Volume → TMB (50 k mid) → V4A (22 k plate) → 1 M Master. CH2 = V1B (100 k) → Gain → TMB (25 k mid) → V2A →
   V2B → 250 k Master.
2. `power` — V5 LTP exactly as drawn → 4× 6L6GC (two pair-models) → ~1k8 centre-tapped OT → 16 Ω tap → speaker
   model. NFB from the speaker node through 47 k into the tail feedback node; Presence = variable R + .1 µF shunt
   on the feedback node (HF), Focus = variable R + 2.2 µF shunt (LF). Knob up = more feedback removed = more of that
   band in the output.
3. `supply` — SS bridge → 100 µF reservoir → 500 R screens → RC-filtered PI (~318 V) and preamp (~250 V) taps.

Tubes: Koren ECC83 for all 12AX7A sections; Koren 6L6GC pair set for the output pairs.

## Controls (page 1)

| Param | Real control |
|---|---|
| Channel | Clean / Lead selector (the amp's channel relays) |
| Volume | CH1 Volume |
| Gain | CH2 Gain |
| Treble / Middle / Bass | the tone controls (drives the selected channel's own stack) |
| Master | channel masters |
| Presence | Presence (HF negative-feedback lift) |
| Focus | Focus (LF negative-feedback lift) |

Page 2 (synthetic): Power Drive, Bias (the fixed-bias trimmer), Tube Feel, Speaker (4/8/16 Ω on the 16 Ω tap),
Output.

## Honest simplifications

- **Reverb is not modelled** — the real spring tank (V3A driver / recovery) is documented but out of scope; use the
  chain's own reverb block after the amp.
- **Ninja boost and CH3 voicing** are not modelled.
- Both channels' EQs are driven by one set of T/M/B params (the real amp has two 3-knob rows; the knobs track).
- Focus's exact cap value is estimated (the drawing's area is ambiguous); its topology — an LF version of the
  presence shunt — matches Rivera's description of Focus as "low-frequency damping".
- The NFB secondary winding sense follows the Twin Reverb model's convention; verified stable in the silence test.

## Verification targets

- Rails ≈ 460 / 445 V; PI plates ~230–260 V; bias ≈ −47 V; matched pair idle ~50–80 mA per pair.
- The NFB loop is stable at all settings (silence AC RMS < 0.01 on both channels).
- Lead channel out-gains the clean channel at matched settings; Presence lifts HF, Focus lifts LF.
