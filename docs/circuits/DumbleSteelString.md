# Dumble Steel String Singer (key `DumbleSteelStringStyleAmplifier`)

Processor: `Source/Effects/DumbleSteelStringStyleAmplifierProcessor.{h,cpp}`.

## Sources

No factory schematic exists. The model follows Ryan Colgan's LTspice model of serial #002, the 1981 Jackson Browne amp
(The Amp Garage / thetonegeek.com, `SSS No 002.asc`, with its bias-point PDF). It was traced from gut shots of the
chassis. The Colgan SSS-100W face and back plates show the real panel. All of these are kept locally in
`~/schematics-audit/sss/`, not in the repo.

Across #001 to #005 the amps share four 6L6GC at ~100 W, Fender-by-Schumacher transformers and a 12AX7 phase
inverter. They also share the SSS signature: DC-coupled cathode followers between the PI and the power grids. #004
onward dropped those followers for a standard LTP drive.

The Steel String Singer is a single-channel clean amp. Dumble's Jazz / Rock switch, PAB and Mid Boost belong to the
Overdrive Special and are not on it. Earlier versions of this model had them; they were removed on 2026-10-31.

## Signal path

| Stage | Parts (#002) |
|---|---|
| V1A | 68K ∥ 68K stopper, 1M leak; 100K plate, 1.5K ∥ 5µ cathode on the 100 Ω LNFB tail (R1 / C2 / R62) |
| Tone stack | straight off V1A's plate. Treble: C1 360p → Treble 250K-A. Bass / mid: R3 100K → C3 .1µ → Bass 250K-A (bottom on R7 1.8K) and C34 .047µ → Middle 100K |
| Guitar / Mic (U34, DPDT) | Guitar: Treble's bottom on C35 4.7nF to ground, the bass wiper merged through R63 100K. Mic: Treble's bottom straight to the bass wiper, R63 lifted |
| Volume | 1M-A on the merged output; Bright (U36) puts C4 250p across it |
| Deep (U35) | bass wiper → R64 270K → R66 1M ∥ (C36 .01 + R67 10K) → R65 270K into the Volume's wiper, bypassing the pot |
| V1B | R69 68K series, 470K leak; 100K plate, 1.5K ∥ 5µ on its own 100 Ω tail (R4 / C5 / R68) |
| Filter bank | C6 .01 + L2 300mH choke. Low: the 39K / 68K / 100K / 180K / 270K / 390K string into R78 12K, tapped through R79 270K. High: 150p to .01µ against the R70 820K / C44 3n node. Both are 7-position rotaries |
| V2A | 100K plate, R81 1K ∥ C46 1µ |
| Follower | V2B (U38); LNFB: C47 .22µ, R83 100K, R82 470K back to V1A's tail |
| Reverb mixer | follower → R84 220K, return → R85 220K, meeting at C49 .01µ |
| Level | 1M-A; Accent (U42) puts C50 1nF across its top leg |
| PI | 12AX7 LTP: 100K / 220K plates, R12 820 + 18K tail, C9 .02 in, 68p plate compensation, NFB from the 16 Ω tap with Presence (25K, U11) |
| Power | 4× 6L6GC as two push-pull pairs, .047µ couplings, 220K leaks, 1.5K stoppers, fixed bias −51 V |
| Supply | rectifier → 220µ + 150K bleeder → 10H choke → 220µ → 2.7K → PI rail → 22K → V2 rail → 15K → V1 rail |

### Reverb

| Stage | Parts |
|---|---|
| Send | from V1B's grid side of the Volume pot (the netlist's `REV` node) → R35 68K → U20 7025: R38 100K, R36 1.5K ∥ C19 5µ |
| Send pot | C20 .001µ → Send 250K-A |
| Driver | R40 10K → 5751 pair in parallel (R39 2.2M leak, R43 2.2K ∥ C25 25µ) → transformer → tank (8 Ω) |
| On / Off | U29 grounds the drive. The tail already in the tank still comes back |
| Recovery | R44 100K → U28 7025: R45 100K, R42 1.5K ∥ C22 5µ on R48 100 |
| Return pot | C27 3000p → Return 100K (linear) |
| Return stages | U39 7025 (R86 100K, R87 1K ∥ C48 1µ) → U40 cathode follower (R88 100K) → R85 into the mixer |
| Return loop | R52 270K + C23 .1µ from U40's cathode back to U28's 100 Ω tail |

## What is modelled

Everything in the main path above, in three NodalCircuit blocks:

- **pre:** V1A through the follower, with the stack, the switches, the filter bank and the LNFB loop.
- **power:** the mixer, Level and Accent; in the full reference also the PI, power stage, OT, speaker and NFB.
- **supply:** sags off the measured currents.

`reducedOrder` (the shipped path) keeps the preamp, the mixer and Level. It replaces the PI and power stage with the
shared behavioural stage, fed from Level's wiper.

The reverb runs linearly around the project's spring engine (`SpringReverbProcessor`, fully wet). The engine is driven
from the Volume pot's wiper and returns into the R85 leg as an ideal source standing in for U40's cathode (~600 Ω
against 220K). It has:

- the two coupling high-passes, C20 into 250K (~640 Hz) and C27 into 100K (~530 Hz). That is why the reverb is
  bright;
- the Send pot's audio law;
- Reverb Off cutting the drive only;
- the return loop. With the stage gains (~58 for U28 with its unbypassed 100 Ω, ~62 for U39) and R52 into ~94 Ω,
  the loop gain is ~1.23 times the Return setting r, so the return follows r / (1 + 1.23 r).

The tank runs one audio block behind, as on the Super-Sonic.

## Interpretations and estimates

- **DC-coupled cathode-follower drivers:** modelled as ordinary capacitive coupling, as on the SLO-100.
- **FET input:** omitted. It is a buffer, and `inputLimit` covers its role.
- **Reverb level:** the netlist models the tank only as a delay, and the 5751 transformer's ratio is not given. The
  overall return gain (`reverbReturnGain` 330) is set so that at full Send and Return a sustained 1 kHz note's wet
  level equals its dry level at the Level pot. The return loop's frequency dependence (C23) and the driver's
  saturation are not modelled.
- **NFB resistor:** estimated at 100K; the per-unit value varies. The PI's small local feedback (R20 2.7K) is
  not added.
- **Middle:** modelled as a rheostat from C34 to ground. On #002 the pot's third leg also reaches V1A's cathode
  tail, which is not included.
- **The toggle by Level:** the faceplate labels it "MID"; it sits where the netlist's ACCENT switch (C50 across
  Level's top leg) is, and is modelled as that.

## Controls

Page 1 follows the #002 faceplate: Volume, Bright, Deep, Guitar / Mic, Treble, Middle, Bass, Filter (Hi), Filter (Lo),
Reverb (On / Off), Send, Return, Accent, Level.

Page 2:

- **Presence Trim:** the internal 25K pot; there is no Presence knob on the panel. Its default 0.35 is the netlist's
  setting.
- **Power Drive, Bias, Tube Feel, Speaker (4 / 8 / 16 Ω), Output, Cab.**

Presets from before 2026-10-31 migrate as follows: Voicing Jazz becomes Mic, PAB and Rock become Guitar, and Mid
Boost is dropped.

## Verification

- **DC:** plates ~470 V, screens ~464 V.
- **Reverb:** wet / dry at full Send and Return is 1.005 (sustained 1 kHz). A note's tail energy at Send and Return
  0.7 is ~150 times that with the reverb off.
- **Reduced vs full:** level 0.40 dB, worst band 6.66 dB (bound 1.5 / 7.5). The reduced stage now has
  `bmLevelTrim` 0.65. The band error is the reduced path's treble (−3.4 dB at rms 0.03, −6.8 dB at 0.40), which is
  left for the reduced-path refit.
- **Unity trim:** +24.07 dB (PedalUnityLevel 0.00). The 220K / 220K mixer halves the dry signal, as on the amp.
- **CPU:** 9.1 % playing, 7.1 % silent, worst block 12.5 % (EffectCostBench).

The tests are in `Tests/DumbleSteelStringStyleAmplifierProcessorTests.cpp`:

- DC convergence, silence and pluck;
- every switch stable;
- the reverb tail and its wet level;
- preset migration;
- the reduced path.
