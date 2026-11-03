# Bogner Uberschall — `BognerUberschallStyleAmplifier`

## Sources

Factory preamp schematic (educational tracing circulated on el34world — carries its own "may contain
errors" disclaimer; values cross-checked where legible) plus the published control layout. ~120 W
dual-channel head, four EL34s. Both channels are modelled (the clean one since 2026-11-02).

## What the amp actually is

Reinhold Bogner's flagship high-gain head. The OD channel chains four 12AX7 gain stages around a
distinctive voiced gain control — a 475K pot wearing a 300pF bright cap and a 330pF shunt — followed
by a TMB+presence network, master, driver, and an EL34 quad. The "tight low end" it's famous for comes
from small 22nF coupling capacitors and a partially-bypassed second stage (470nF instead of the usual
4.7–22µF): less low-frequency energy reaches the saturation stages, so the amp can be pushed hard
without the flub of bigger-coupled high-gain designs.

## What is modelled

- V1A (R5 180K plate, R6 1.8K + C3 4.7µF; R2 68K stopper, R1 100K leak) → CX2 100nF → **Gain**
  (P1 475K + C202 300pF bright + K4 330pF shunt) → KX2 **22nF** → V1B (R7 100K, R10 2.4K + K3 470nF;
  R8/R9 68K) → 22nF → V2B (100K, 1.8K + 4.7µF) → 22nF → V3A (100K, 1.8K + 4.7µF) → V3B cathode
  follower.
- TMB stack (150pF treble cap, 56K slope, .022 bass/mid, 240K treble / 720K bass / 25K mid) + 1M Master.
- 12AX7 LTP PI (82K/100K plates, NFB est. 56K + 25K presence) → .047µF couplings → 220K leaks → 1.5K
  stoppers → 4xEL34 pairs, fixed bias −45V, ~460V plates.
- EL34 Koren fit is the codebase's Super Lead single-tube set (mu 8.11, Ex 1.5, kg1 1201, kg2 3720,
  kp 100, kvb 24) with standard pair scaling — a real published fit, not an estimate.
- Supply sag, resonant speaker, `reducedOrder` behavioural stage.

## Clean channel (2026-11-02)

Lower half of the same tracing, its own netlist (`preClean`), solved only while it sounds:

- V1A rebuilt (shared input stage) → 100nF → R303 499K ∥ C305 680p, plus C306 3n3 → RX5 499K → **Bass**
  P8 220K rheostat; R302 340K with C303 150p across → R301 100K ∥ C301 470p → **Gain** P7 390K log
  (C302 82p bright) → V4b (100K plate, R26a 1K + K16 10µF).
- CX5 47n → R305 100K → C4 → **Middle** P9 22K rheostat; C309 → **Treble** P10 240K, its bottom on
  C308 100n + R306 220K; wiper → C310 → **Presence** P11 270K rheostat + R320 22K (a treble shunt) and
  → C311 100p ∥ R308 360K → **Volume** P12 430K → CLEAN OUT.
- C309, C4 and C310 are blank on the tracing: 470p, 22n and 1n are **ESTIMATED**. A 10M resistor on
  node Q is a numerical DC reference only (P/Q see nothing but capacitors).
- CLEAN OUT enters at the Master's top through the channel switch (an ideal source riding the OD stack's
  own DC, so the PI coupling cap never steps); the NFB presence belongs to the OD channel only.
- Stage swings for a 0.1 V 165 Hz note (test log): V1a 7.4 Vpp, V4b 8.7 Vpp, clean out 0.14 Vpp — against
  23 Vpp at the Master's top from the saturated OD channel.
- Level match (see `ChannelLevelMatch.md`): Clean sat −46.7 dB (E3) / −62.9 dB (guitar DI) under the OD
  channel; ~16 dB of that is the OD Volume being normalised to unity at noon while the clean Volume keeps
  its log law. The 54.8 dB midpoint is an eased **output** trim — injected before the PI it drove the
  EL34s to ~20% THD and the clean channel stopped being clean. After: +8.1 (E3) / −8.1 dB (DI), clean THD
  0.05% (0.05 sine) to 1.3% (0.6 sine). (The first cut used 51.1 dB: its "DI" figure was really the E1
  tone, measured on a test build from before SWITCH_DI existed.)

## Deliberate simplifications

- Optocoupler/relay switching (the channel change is a ~10 ms fade + a switch at the Master's top), FX-loop buffers (V5a/V5b) and the send/return: not
  modelled — the loop is bypassed as it is on the real amp with nothing plugged in.
- The Uberschall's interactive presence network is approximated by the family's standard NFB-presence
  block; the NFB resistor is an estimate.
- A couple of mid-chain plate loads sit in the schematic's blurred region; 100K is used (marked).

## Controls

Page 1 mirrors the real panel: Channel (Clean / Overdrive), then the selected channel's own Gain, Volume,
Treble, Middle, Bass, Presence (per-channel knobs via `ChannelKnobMemory`, saved in the state), and the
shared Master. The OD Volume is a 10K pot after its stack on the real amp; it scales the OD drive into the
linear stack, normalised to the old fixed level at noon.
Page 2 (synthetic): Power Drive, Bias, Tube Feel, Speaker (4/8/16Ω), Output.

## Stability / verification

- kg1×40 on PI triodes and EL34 pairs -- REMOVED (see CircuitFamilies.md's dead-reference repair
  section); DC plates ~460V; unity trim for PedalUnityLevel.
- `Tests/BognerUberschallStyleAmplifierProcessorTests.cpp`: DC, silence settle, pluck, clean-channel stage
  swings (logged).

## Reduced path refit (2026-11-01)

The shipped reduced path was refitted against the full netlist with the AmpFullVsReducedConsistency settings: chords at
rms 0.03 and 0.40, three bands, and a coordinate search over the stage's constants.

| | Level (dB) | Worst band (dB) |
|---|---|---|
| Before | 0.58 | 11.80 |
| After | 1.18 | 5.15 |
| Bound | 2 | 6 |

Changed: `bmGain0` 17, `bmYmax` 0.19, `bmKneeN` 6, `bmShelfHz` 200, `bmShelfHfGain` 2.2, new output trim `bmLevelTrim` 0.484. The unity trim moved −0.14 → −7.21 dB to keep the amp at unity.


## Phase inverter rewired (2026-11-02)

The power-amp tracing (sheet 2) biases both PI grids from the tail node T through 1M:
- R112 runs to V6a's grid, R122 to V6b's.
- C105 100n AC-grounds V6b's grid to T, and C120 220p runs from V6a's grid to the cathodes.
- The tail is R114 470 → T → R123 10K → J → R121 4.7K.
- The feedback lands on J, through the optocoupler + C117 22µ + R126 10K. The cell's lit resistance is not on the
  tracing, so feedbackResistor 56K stands for the whole leg.
- The Presence is P5 2.2K behind C208 + C209 (12.2µ), shunting J.

The old netlist returned V6b's grid to the ~0 V feedback node while the cathodes sat near 30 V. V6b was cut off, so
the loop and the Presence were dead.

| Check | Before | After |
|---|---|---|
| PI plates at idle | 287 / 433 V | 289 / 280 V |
| AmpFullVsReduced, worst level | — | 1.34 dB (bound 2) |
| AmpFullVsReduced, worst band | — | 4.05 dB (bound 6) |

The reduced path the app runs is unchanged; it still tracks the corrected reference within those bounds.
