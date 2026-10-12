# Diezel VH4

The Diezel VH4 is a 4-channel German high-gain head: Clean / Crunch / Mega / Lead, each with its own
Gain/Treble/Middle/Bass/Volume stored per channel (the amp's MIDI memory), plus a global Master and Presence + Deep
(global negative-feedback voicing). Output section: four KT77s (the EL34 family) into a 4x12.

## Sources

- No verified factory schematic is public for the VH4 (Diezel does not release them). The model follows the
  documented VH4 topology — four progressively hotter channel structures on a shared power section — with
  component values scaled from the era's British-style cascodes (marked *estimated* below), the way the repo's
  other un-schematic'd amps are handled.
- Service/bench measurements published in the amp-technician press and Zottola's notes on Diezel: ~430 V plates,
  ~420 V screens, ~310 V PI rail, ~270 V preamp rail, fixed bias ≈ −44 V, silicon rectification.
- Owner's manual for the control layout: per-channel Gain/TMB/Volume, global Master, Presence, Deep.

## What the real circuit is

- **4 channels**, each its own preamp structure:
  - **Clean** — 2 triode stages at high headroom, Fender-leaning tone stack (47 k slope, 250 pF treble cap,
    22 nF bass / 47 nF mid, 50 k mid max).
  - **Crunch** — 2 stages with a cold-clipper second stage, tighter stack (33 k, 270 pF, 47 nF/100 nF, 25 k mid).
  - **Mega** — 3 stages (22 k, 470 pF, 22 nF/47 nF, 25 k mid), 820 R cold second cathode.
  - **Lead** — 4 stages on the Mega stack plus a 2.7 k cold fourth cathode.
- **Long-tailed-pair PI** on the ~310 V tap (estimated values).
- **4× KT77** fixed-bias push-pull quad, ~430 V plates / ~420 V screens, ~5 kΩ class-AB output transformer.
- **Global negative feedback** with **Presence** (HF NFB lift) and **Deep** (LF NFB lift — Diezel's name for the
  resonance/focus control).
- Silicon bridge rectifier; the sagging supply character comes from the output stage, not a tube rectifier.

## What is modelled

Same structure as the Rivera model, extended to four preamp netlists:

1. `pre[4]` — one netlist per channel (stage counts and tone stacks above), each ending in its own Volume and a
   shared global Master inside the same netlist so the reduced-order path keeps working. Only the selected
   channel's netlist is solved per sample; per-channel `DynamicState` rest slots preserve warm bias on switch,
   with the usual ~4 ms `chanFade` declick.
2. `power` — LTP PI → 4× KT77 (Koren EL34-family fit: mu 8.11, ex 1.5, kg1 1201, kg2 3720, kp 100, kvb 24, the
   pair-doubling convention — kg1/kg2 halved, grid conductance doubled) → OT → speaker taps → NFB with
   Presence + Deep legs.
3. `supply` — SS bridge (15 Ω effective) → ~430 V plates → 470 Ω screens (~420 V) → 10 kΩ PI (~310 V) → 10 kΩ
   preamp (~270 V).

reducedOrder ships on: the behavioral power stage reuses the K100-fit constants with the sag-rail table rescaled
to 430 V (`bmSagRail[9]` starting at 430); `DZL_POWERCAL` is the gate for a real per-amp calibration if a measured
unit becomes available.

Per-channel knob memory (`ChannelKnobMemory`): Gain/Treble/Middle/Bass/Volume carry per-channel values like the
real MIDI store, and are saved/restored through `getState`/`setState` (the `ScopedSuspend` pattern).

## Controls (page 1)

| Param | Real control |
|---|---|
| Channel | Clean / Crunch / Mega / Lead |
| Gain / Treble / Middle / Bass / Volume | the selected channel's own knob row (per-channel MIDI memory) |
| Master | global Master |
| Presence | Presence (HF NFB lift) |
| Deep | Deep (LF NFB lift) |

Page 2 (synthetic): Power Drive, Bias (fixed-bias trimmer), Tube Feel, Speaker, Output.

## Honest simplifications

- The four channel netlists use British-cascade-typical values scaled to the VH4's rails — the real channel
  voicings are Diezel trade secrets; the *structure* (2/2/3/4 stages, progressively hotter, distinct stacks)
  matches what the manual and teardowns document, the exact component values are estimated.
- MIDI preset store/recall beyond the per-channel knob memory is out of scope (the chain's own preset system
  covers it).
- The VH4's internal FX loop, channel-insert, and MIDI switching hardware are not modelled.

## Verification targets

- Rails ≈ 430 / ~415–420 V (470 Ω screen dropper); bias ≈ −44 V; matched pair idle ~55 mA per tube
  (~110 mA per push-pull pair) — KT77 at these rails runs hotter than 6L6.
- NFB stable at all settings (silence AC RMS < 0.01 on all four channels).
- Per-channel output rises Clean → Crunch → Mega → Lead at matched knobs; each channel has its own stage count
  and stack values, so their waveshapes differ, not just their loudness.
