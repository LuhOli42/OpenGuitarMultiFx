# Bogner Uberschall — `BognerUberschallStyleAmplifier`

## Sources

Factory preamp schematic (educational tracing circulated on el34world — carries its own "may contain
errors" disclaimer; values cross-checked where legible) plus the published control layout. ~120 W
dual-channel head, four EL34s. This model covers the **overdrive channel**.

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

## Deliberate simplifications

- Clean channel, optocoupler/relay switching, FX-loop buffers (V5a/V5b) and the send/return: not
  modelled — the loop is bypassed as it is on the real amp with nothing plugged in.
- The Uberschall's interactive presence network is approximated by the family's standard NFB-presence
  block; the NFB resistor is an estimate.
- A couple of mid-chain plate loads sit in the schematic's blurred region; 100K is used (marked).

## Controls

Page 1 mirrors the OD channel: Gain, Treble, Mid, Bass, Presence, Master.
Page 2 (synthetic): Power Drive, Bias, Tube Feel, Speaker (4/8/16Ω), Output.

## Stability / verification

- kg1×40 on PI triodes and EL34 pairs; DC plates ~460V; unity trim for PedalUnityLevel.
- `Tests/BognerUberschallStyleAmplifierProcessorTests.cpp`: DC, silence settle, pluck.
