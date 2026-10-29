# Super-Sonic 22 (key `SuperSonicStyleAmplifier`)

Processor: `Source/Effects/SuperSonicStyleAmplifierProcessor.{h,cpp}`.

## Source

Fender's service diagram for the Super-Sonic 22, drawing 0078327000 rev A (08-JAN-10), three sheets at 600 dpi
(thetubestore.com, `Fender-Super-Sonic-22-Schematic.pdf`). It prints every rail and the test voltages for a 1 kHz input.
Kept locally in `~/schematics-audit/supersonic/`, not in the repo. The original Super-Sonic 60/100 service manual is
there too; it is a different amp (6L6s) and is not what this model follows.

## Signal path

The channels and the Normal / Fat voicing are switched by relays K1-K7.

| Stage | Parts (sheet 1) |
|---|---|
| Input, V1A | R1 10K, R2 1M; V1A 12AX7: R3 100K on W, R4 1.5K ∥ C1 22µ |
| Vintage stack | from V1A's plate: C4 250p, R8 100K, C5 .1, C6 .047; Treble R9 250K (30 % audio), Bass R10 250K (rheostat), R11 6.8K fixed mid; Volume R12 1M (30 % audio) on the Treble wiper |
| Normal / Fat at V1A | Normal: C7 47p across the Volume's top leg (K3-A). Fat: C3 220p from V1A's plate to ground (K3-B) |
| Burn input | C2 .0022 (R5 1M) → R6 15K → Gain 1 R31 250K with C15 470p to ground; K1-B grounds it on Vintage |
| V1B | K1-A picks the Vintage Volume or Gain 1 wiper → R14 1.5K (R45 470K) → V1B: R15 100K on W, R16 1.5K ∥ C8 2.2µ, plus C19 22µ on Burn (K6-A) |
| Vintage, Normal | C9 .22 → C10 .022 → R22 3.3M ∥ C11 10p → R23 270K: a ~22 dB loss, then V2B open loop |
| Vintage, Fat | C9 → R20 47K → node with R19 120K → C12 .01 → V2B, and **R24 470K from V2B's output back to that node**: V2B in shunt feedback, gain ≈ 470K / 47K |
| Burn | C16 .22 → Gain 2 R35 250K (+ R36 20K) with C18 47p, R38 10K on the wiper, R37 110K, R39 1.5K → V2A (R40 100K on X, R34 1.5K ∥ C17 22µ) → C20 .047 → R42 470K / R43 100K → V2B |
| V2B | R26 100K on X, R27 1.5K ∥ C13 2.2µ, plus C21 22µ on Burn (K7-B); grid leak R25 470K; C14 .22 (R109 1M) |
| Vintage output | R28 100K / R29 15K |
| Burn output | stack C22 150p, R46 120K, C23 .15, C24 .022; Treble R47 250K lin, Bass R48 250K, Middle R49 25K lin; R50 220K (C25 680p to ground) → Volume R51 250K (30 % audio) |
| Mixer | K2-B's output (R30 1M) → R75 3.3M; the reverb return (V4A plate → C96 .0033 → Reverb R166 100K lin → R167 1M); R76 220K → R126 10K → V4B (R77 100K on W, R78 1.5K ∥ C38 22µ) → C39 .1 → R80 22K → power amp input (R79 1M, R127 1M) |
| Reverb send | K2-B's output → C92 470p (R161 1M) → V3 12AT7 driver → tank → V4A recovery |
| PI (sheet 2) | C68 .01 → V5A (R129 1M); 12AT7 LTP, R134 91K / R135 100K on Y, R130 470 → R128 22K → feedback node → R133 47; R131 1M and C69 .1 on V5B's grid |
| Power | C71 / C72 .1, R136 / R137 220K to the bias (−43.7 V), R138 / R139 1.5K, 2× 6V6GT, R142 / R143 470 screens on Z, 1 Ω cathode sense resistors |
| NFB | R132 820 from the speaker into the PI's feedback node (R133 47 to ground) |
| Supply (sheet 2) | B+ 420 (plates) → L1 → Z 418 (screens) → R148 4.7K → Y 376 (PI) → R150 10K → X 317 (V2) → R151 10K → W 280 (V1, V4) |

## What is modelled

All of the above, in NodalCircuit blocks cut at high-impedance points; each upstream block carries a passive copy of
the next block's input as its load.

| Block | Contents |
|---|---|
| A | input, V1A, both input networks, K1, V1B |
| B | the Vintage Normal / Fat networks, Gain 2 and V2A, V2B, both output networks, K2 |
| C | the mixer and V4B |
| power | PI, 6V6s, OT, speaker, NFB (full reference); `reducedOrder` replaces it with the shared behavioural stage, driven from C68 into the PI's 1M |

- **Reverb:** the tank and its driver / recovery are the project's spring engine (`SpringReverbProcessor`, fully wet),
  fed the send through C92 / R161 and returned at V4A's plate at 0.19 of the send level. That ratio is the drawing's own
  test voltages: 88 mV at the channel output, 17 mV at V4A's plate. The return then goes through the real C96, Reverb
  pot and R167 into the mixer. The tank runs one audio block behind, which is shorter than a real tank's own delay.
- **Effects loop:** not modelled. With nothing plugged in, relay K5 bypasses it and its op-amps are out of the path.
- **Footswitch, LEDs, mute FETs:** not modelled. The mute FET Q1 is off in normal use.

## Interpretations and estimates

- **Output transformer** (P/N 0041318000): not specified. Taken as ~6.6K plate to plate, 8 Ω, the usual load for two
  6V6GTs at ~415 V.
- **Fat's node** as V1B sees it: R20 47K into R19 120K in parallel with V2B's feedback. That parallel is roughly 12K
  (470K / (1 + gain)). This only matters for block A's load copy.
- **Tubes:** Koren's published 12AX7 set. The 12AT7's and 6V6GT's kg1 are fitted to the drawing's printed operating
  point, because the published values ran both hot. The 12AT7 goes from 272 to 1700, which brings the PI's cathodes to
  ~71 V against the printed 70.3 V. The 6V6GT goes from 1460 to 1150, which gives 24 mA per tube at −43.7 V, the
  printed 24 mV across each 1 Ω. The Deluxe Reverb's 12AT7 needed the same order of change (3000).

## Controls

Page 1 is the front panel: Channel (Vintage / Burn), Fat (Normal / Fat), Vintage Volume, Treble, Bass; Burn Gain 1,
Gain 2, Treble, Bass, Middle, Volume; Reverb. Page 2 (synthetic): Power Drive (a 1M pot in place of R129, so at full it
is the real amp), Bias, Tube Feel, Speaker (4 / 8 / 16 Ω), Output, Cab.

## Verification

| | Model | Drawing |
|---|---|---|
| Rails B+ / Z / Y / X / W | 420 / 418 / 376 / 316 / 278 V | 420 / 418 / 376 / 317 / 280 V |
| PI cathodes | ~71 V | 70.3 V |
| 6V6GT current | 24 mA each | 24 mV on 1 Ω |

- **NFB:** closing the loop lowers the small-signal gain by ~3 dB. R132 820 into R133 47 is light feedback.
- **Reduced vs full:** level 0.10 dB, worst band 1.39 dB (bound 1 / 2). The reduced stage is `bmGain0` 10, `bmYmax`
  0.08, shelf 0.65, trim 1.1.
- **Channel balance** at the default knobs, 0.2 V in: Vintage 0.134, Vintage Fat 0.138, Burn 0.131.
- **Unity trim:** −20.19 dB (PedalUnityLevel 0.00).
- **CPU:** 12.1 % playing, 9.1 % silent, worst block 15.5 % (EffectCostBench).

The tests are in `Tests/SuperSonicStyleAmplifierProcessorTests.cpp`:

- DC against the printed rails and voltages;
- silence and a hot pluck on all four channel / Fat combinations;
- NFB sign;
- Burn compression and Fat's gain;
- the reverb tail.
