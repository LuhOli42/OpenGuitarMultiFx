# Blues Breaker-Style Overdrive

Display name **"Blues Breaker-Style Overdrive"** (the original is the Marshall Blues Breaker, 1992). Registry key
`BluesBreakerStyleOverdrive`, class `BluesBreakerStyleOverdriveProcessor`. Runs on [`NodalCircuit`](./NodalCircuitSolver.md):
two blocks per channel (cut at the first op-amp's output), each with one `addOpAmpMacro` TL072. 2x oversampling at the High
tier (`Orders {0, 0, 1}`). Cost 3.9% of a core at the default quality.

## Source
The 1992 schematic in the GM Arts / Stomp Box Schematics collection (`a_mbb1.png`, a redrawn 539 x 247 image, read at 3x
zoom), cross-checked with the Aion FX Cerulean documentation (parts list: 1N914 x 4, TL072, R 1M/3k3 or 27k/4k7 or 33k/6k8/
220k/10k/1k, C 10n/47p/100p/220n...). **The two disagree on the (-) leg of the gain stage**: the 1992 drawing has 27K and 33K,
Aion's stock circuit 3k3 and 4k7 ("the very first version actually uses 27k and 33k ... gives the unit lower drive"). This
model is the first version (27K / 33K), the original pedal. The later 3k3/4k7 version has ~7x the first stage's maximum gain;
it is a constants change if wanted.

## Topology
1. **Gain stage** (TL072 A, non-inverting): 10 nF -> (+) with 1M to the 4.5 V bias; (-) leg = **27K in parallel with (33K + 10 nF),
   then 10 nF to the bias** (two corners, so the gain rises in two steps); feedback = the **Drive pot (100K linear) from (-)
   to its wiper (the op-amp output)** || 100 pF. Gain = 1 ... ~7.7 (the two shelves). The pot's far half is a series resistance
   into stage 2 (same arrangement as the Guv'nor's Gain pot).
2. **Clipping stage** (TL072 B, inverting): 0.1 uF + 10K into (-); (+) at the bias; feedback = **220K in parallel with (6.8K +
   four 1N4148 diodes, two in series each way)** -- the swing is limited to ~+-1.2 V, softly, by the 6.8K in series with the pairs.
3. **Tone** (25K log): 1K from the op-amp, the pot's bottom end through 10 nF to the bias, wiper -> 6.8K -> 10 nF to the bias;
   **Volume** (100K log, bottom end to the bias) -> 0.1 uF -> output (1M to ground).

## Model
TL072 (A0 2e5, GBW 3 MHz, 100 ohm out, rails 1.5-7.5 V, `addOpAmpMacro` with its windup clamp); 1N4148 (Is 2.52 nA, N 1.752);
4.5 V bias as an ideal source (the schematic marks it without drawing how it is made). Tone: wiper up = bright; Tone/Volume taper
~knob^2 (the "log" pots); Drive linear.

## Verification (`Tests/BluesBreakerStyleOverdriveProcessorTests.cpp`)
* DC: both op-amp outputs at 4.5 V.
* Stage 1 gain vs the closed form (two-corner leg, TL072 pole, feedback || 100 pF), 3 Drive x 3 frequencies: within 0.25 dB
  (1.0 / 2.8 / 4.8x at 1 kHz).
* Stage 2 gain (220K over the pot's series segment + 10K) vs the closed form: within 0.15 dB (2.0 / 3.6 / 21x).
* The feedback diodes limit stage 2's swing to ~1 V (0.97 V at Drive max, 0.1 V in), symmetric to 3%.
* Tone: wiper up raises 5 kHz relative to 200 Hz by 6.6x; Volume monotonic; no failed solves at any Drive under a hot sine; stereo.
* Unity level: registry trim +5.28 dB.
* **Not verified against the real pedal's response**: no published gain/frequency figures were found.

## Not modelled
The schematic's pedal-specific bypass (a true bypass on the real one), the mods (clipping switch, presence, bright cut), TL072 slew,
input R 2.2M (anti-pop).
