# Diezel VH4

The Diezel VH4 is a 4-channel German high-gain head: Clean / Crunch / Mega / Lead. Each channel has its own
Gain / Treble / Middle / Bass / Volume, stored per channel in the amp's MIDI memory. There is a global Master,
plus Presence and Deep in the global negative feedback. The output section is four EL34-family tubes into a 4x12.

## Sources (rebuilt 2026-11-03)

- **Preamp**: "VH4 v1.0" (Kojak, Dec 2012), a vector drawing of the whole preamp, all four channels to the FX send.
  Downloaded by the user as `diezel_vh4_preamp_sch.pdf`; a copy sits in `~/schematics-audit/diezel/`.
- **Power amp**: Caphead / WJD's hand-drawn "VH4 Output Rough Draft" (11/2012, 2000-01 era), from the user's
  `Diezel.rar`. The PI and the NFB / Presence / Deep networks come from it. The values it marks "?" are used as
  drawn and flagged below.
- **Service manual**: 35 mA idle a tube.

The old model followed no drawing at all. Its estimated "British cascade" channels left the amp ~31 dB short of drive
at the defaults; the registry trim went from +29.45 to −1.47 dB. That is the "fuzz that doesn't distort properly" the
user reported.

## The real circuit (preamp drawing)

All stages are 12AX7 with 120k plates on +367 V. V1a is the input stage for every channel: In-1 → 1M ∥ 100-200p →
V1a (2k2 + 47µ). Its plate feeds all four channel input networks at once. Relays then pick one path to V3a:

| Channel | Path after V1a | Gain stages before V3a |
|---|---|---|
| CH1 Clean | C3 220n → R4 / R5 / R6 470k chain with C4 2.2n / C5 / C6 220p, R7 220k to ground, C7 68p + 10M → CH1 Gain 250k | 1 (V1a) |
| CH2 Crunch | C8 10n / R8 220k → R9 3M3 ∥ C9 680p → CH2 Gain → V2b (680R + 1µ) → C11 4.7n → R12 470k (C12 470p + 10M) → R13 1M | 2 |
| CH3 Mega | C13 1n → R14 220k → CH3 Gain → R21 47k → V2a (R22 3M3, 680R + 1µ) → C18 4.7n | 2 |
| CH4 Lead | C14 1n → R15 220k → CH4 Gain (R16 220k) → V1b (3k3 + 1µ) → C16 2.2n → R19 470k / R20 220k → R21 → V2a | 3 |

After the channel paths:

- **V3a** (680R + 2.2µ) is reached through R24 / R25 1M and R26 470k. V3b is a DC-coupled cathode follower
  (R29 120k), then C20 220n to the tone-stack bus (R30 1M).
- **Four identical tone stacks**, one per channel: 10k → S → 560p → Treble 220k. Treble's bottom runs to Bass 1M,
  then Middle 22k, then ground. From S, 39k → M, with 22n from M to the Bass wiper and 22n to the Middle wiper. The
  Treble wiper feeds R 470k → Volume 470k.
- **Volume wiper** → relay → R35 56k (CH1) or 680k (CH2-4) → V4a (640R unbypassed) → V4b cathode follower → FX send /
  series return → Master.

## The real power amp (rough draft)

- **PI**: 12AX7 LTP with 120k / 120k plates. .022 from the Master into grid 1. Both grids return through 1M to the
  tail node T, which sits under the 680R cathode resistor. T → 10k → J → 4.7k(?) → ground. Grid 2 is AC-grounded
  to J by .022.
- **Feedback**: the speaker → 10k(?) ∥ 470p → 4.7k → J.
- **Presence**: J → .1 → 25k(?) rheostat → ground. This is an HF shunt on the loop.
- **Deep**: J → .1 → 10k(?) pot → a 1-3 H inductor → ground. This is a series-LC shunt that resonates near 350 Hz,
  so Deep lifts the low mids it stops feeding back.
- **Output tubes**: .22 couplings, 220k grid leaks to the bias supply, 10k stoppers per tube.
- **Bias**: −47 V, which gives ~36 mA a tube (service manual: 35 mA). The old −44 V ran 56 mA a tube.

## What is modelled

NodalCircuit allows 48 unknowns per block, so the chain is split in four:

1. **`pre[4]`**: one netlist per channel, from V1a to V3b's cathode and the tone-stack bus.
   - The other channels' input networks stay on V1a's plate as simple RC equivalents.
   - The four stacks load the follower as one 12.25k + 88n leg.
   - V3b is a real triode, so the follower clips the way the real one does, with no ideal buffer.
   - Only the selected channel's netlist is solved per sample.
2. **`post`**: one shared netlist with the stack, Volume, the R35 / R680k mixer resistor, V4a, V4b and the Master.
   - It is fed by the bus voltage as an ideal source.
   - The four stacks have identical values, so the visible (selected) channel's knobs drive it.
3. **`power`**: PI → 4× EL34-family tubes (two pairs) → OT → speaker, with the NFB / Presence / Deep networks.
   This block runs in the full model only.
4. **`supply`**: the solid-state supply.

reducedOrder replaces `power` with the behavioural stage. Its constants are unchanged; `bmLevelTrim` 1.48 re-levels
it to the rebuilt reference.

AmpFullVsReduced per channel (bounds 4 / 6 dB):

| Channel | Level | Band |
|---|---|---|
| Clean | 0.85 dB | 1.26 dB |
| Crunch | 2.22 dB | 2.40 dB |
| Mega | 0.07 dB | 1.27 dB |
| Lead | 0.42 dB | 1.12 dB |

## Controls (page 1)

| Param | Real control |
|---|---|
| Channel | Clean / Crunch / Mega / Lead |
| Gain / Treble / Middle / Bass / Volume | the selected channel's own knob row (per-channel MIDI memory) |
| Master | global Master |
| Presence | Presence (HF NFB shunt) |
| Deep | Deep (low-mid NFB shunt) |

Page 2 (synthetic): Power Drive, Bias (fixed-bias trimmer), Tube Feel, Speaker, Output.

## Simplifications / open points

- **Values marked "?" on the hand-drawn power amp are used as drawn**: the NFB 10k, the 4.7k at J, Presence 25k,
  Deep 10k and the 2 H inductor. The .001 near the PI plates is not modelled; its exact connection cannot be read.
- **Not modelled**: the FX loop (never modelled), MIDI switching, and the relay mute FETs. The open switches across
  C7 / C12 are left open, as drawn.
- **Lead at full Gain**: with a 0.5 V input it has a 0.04% solver failure rate (a few recoveries). Realistic levels
  run clean.
- **CPU** (reducedOrder, Lead): 18%, from five triodes in the preamp netlist and two in `post`.

## Levels

- Every channel's knobs start at noon.
- The four channels land within ~2.6 dB of each other at the defaults, measured with E3 and the guitar DI. So
  `channelLevelMatchDb` stays at 0, with the eased trim in place for later use.
- Registry trim: −1.47 dB, which puts PedalUnityLevel at 0.0 dB.
