# Ampeg SVT-Style Amplifier

`SVTStyleAmplifierProcessor` (`Source/Effects/SVTStyleAmplifierProcessor.{h,cpp}`) — a component-level model
of the Ampeg SVT bass amplifier, after the SVT-CL (the modern Classic reissue, the longest-running production
revision and the one with a complete published service schematic).

## Sources

- Ampeg SVT-CL service manual, schematic pages `07S519` (PWA preamp), `07S419-03` (power amp), and the
  output-tube PCB — component designators used below are the schematic's own.
- Ampeg published specifications (1969 SVT data sheet): Treble ±12 dB @ 4 kHz, Bass ±12 dB @ 40 Hz,
  Mid ±20 dB @ 220 / 800 / 3000 Hz (3-position switch), Ultra-Hi +20 dB @ 8 kHz, Ultra-Lo −20 dB @ 600 Hz;
  300 W RMS into 4 or 2 ohms; six 6550 output tubes; sensitivity 0.019 V for full power.
- Norman Koren's tube parameters ("Improved vacuum tube models for SPICE", Table 1): 12AX7, 12AU7 and 6550
  sets used verbatim; see "Tubes" below.
- Mercury Magnetics ASVT-O spec sheet (secondary taps 2 / 4 ohm — primary impedance is NOT published).

## What the SVT-CL actually is (relevant to the model)

Three physically separate chassis sharing one umbilical:

- **Preamp:** an NE5532 input buffer (D5/D6 clamps, R1 22k), then V1:B 12AX7 gain (R7 100k plate to ~280 V,
  R8 1.5k + C2 22 µF cathode, plate ≈ 235 V), a passive Ultra-Lo / bright network, the P1 1M Gain pot,
  V1:A 12AX7 gain (R27 100k, R28+R29 1.5k + C10 .033 cathode, plate ≈ 240 V), the Baxandall-style tone
  section (P2 Bass 1M-A, P4 Treble 1M-A, R48 220k, C23 470 pF, C20/C21 .001/.01, R37 22k, R30 100k,
  C27 .0047, R32 100k), V2:B 12AX7 recovery gain (R43 100k, R44 1.5k + R45 2.2k, C12 10 µF), the
  tapped-inductor mid circuit (C17 .033, L1 800/300/100 mH, S2 select, P3 Mid 50k-L), R36 470 into the
  P5 50k-L Master, and V2:A 12AX7 cathode follower out (R15 4.7k + R16 220k, C26 .68, R33 1M).
- **Power amp:** V1 12AX7 phase splitter, V2/V3 12AU7 driver stages (common-cathode, R21/R29 47k 5 W plate
  loads to +365 V, R20/R28 1.8k cathodes, C9 1 µF between them), each plate DC-coupled through a ~600k
  resistive level-shifter (300k+120k above the tap, trim+tails below to −180 V) that lands the six 6550 grids at
  ≈ −45 V; screens +365 V via 220 ohm; plates ~660–690 V on the output-transformer centre tap.
- **Supply:** ~690 V plate rail, ~365 V screen/driver rail, +345 V preamp/PI rail, ±180 V driver-return
  rails, and the separate heater/bias windings. Protection/bias-indicator op-amps (TL072/TL074), 1N3070
  clamps, per-tube 10 ohm sense resistors and the standby/fault logic are metering, not tone — not modelled.

## What is modelled

```
PREAMP BLOCK (always solved)
  in --R1 22k--R3 100k-- g(V1:B);  R4 3.3M leak
  V1:B 12AX7: plate --R7 100k-- rail(345);  k --R8 1.5k || C2 22u-- gnd
  plate --C3 .1-- na --R11 100k-- gainPotTop
       na --R19 220k-- gnd;  na --C4 56p-- gnd
       Ultra-Lo (switched): na --R20 330k-- u --R21 220k-- gainPotTop;  u --C8 .0047-- gnd;  u --R22 3.3M-- gnd
  gainPot (P1 1M-A) wiper --C9 .1-- g(V1:A);  R26 1M leak
  V1:A 12AX7: plate --R27 100k-- rail;  k --R28 1.5k--R29 1.5k-- gnd, C10 .033 across R29
  plate --C11 .1-- ti
  TONE:  ti --R48 220k-- P2top;  P2bot --R37 22k-- gnd;
         P2wiper --C20 .001 [+ C21 .01 Ultra-Hi] --R30 100k-- t2
         ti --C23 470p-- t2;  t2 --P4(track)-- C27 .0047 -- gnd;  P4wiper = out
  out --R32 100k-- C16 .1 -- g(V2:B);  R40 470k leak
  V2:B 12AX7: plate --R43 100k-- rail;  k --R44 1.5k--R45 2.2k-- gnd, C12 10u across R45
  plate --C5 .1-- midNode (R12 100k + R13 8.2k bias string to rail)
  MID TRAP: midNode --C17 .033-- tap --P3(50k)-- [C,L series] -- gnd;  tap --470k-- gnd
            S2: 220Hz=800mH+.68u / 800Hz=300mH+.15u / 3kHz=100mH+.033u
  midNode --R36 470-- P5 Master(50k-L) -- gnd;  wiper --C6 .1-- g(V2:A);  R14 220k leak
  V2:A 12AX7 CF: plate -- rail;  k --R15 4.7k--R16 220k-- gnd;  k --C26 .68-- R33 1M -- preOut

POWER BLOCK (the global feedback loop lives here; replaced by a fitted curve when reducedOrder is set)
  preOut --C .1-- g7 --R7 1k-- g(PI:A);  R8 470k leak (coupling cap -- without it the 1k holds g1
    at the source's 0 V while g2 floats to bn, unbalancing the pair on silence)
  PI 12AX7 LTP: pa --R12 100k-- +345;  pb --R15 68k-- +345;  shared k --R14 220-- bn --R13 47k-- -180 V
  pa --C8 2.2-- gs --47k stopper-- g(drvA);  pb --C11 2.2-- gs --47k stopper-- g(drvB);  470k grid leaks
  DrvA/DrvB 12AU7: plate --47k-- +365;  k --1.8k-- gnd;  C9 1u k-to-k
  Level shift:  plate --300k-- m --120k-- tap --~265k-- -180 V   → tap ≈ -45 V, ~0.31 x plate signal
                (0.1 uF bypass on each tap; the Bias knob sweeps the tap resistor 180k..350k)
  tap --47k-- composite 6550 grid (each bank of three = one pentode with 3x current)
  OT:  CT --+690;  two halves -> plates;  secondary -> speaker (2/4/8 ohm)
  NFB:  speaker --Rfb 150k-- fp --C 2.2-- g2(PI:B)   [+ stray cap to gnd]
```

Supply model: rectified ~690 V node (plate), a ~365 V node (screens + drivers, separate winding modelled as
its own source + series resistance), +345 V after R49 8.2k for preamp + PI, and a fixed −180 V.
The schematic's +345 V is the loaded value, so the constant sink through R49 is the implied 2.4 mA
(365-345 V / 8.2k) rather than a sum of the preamp/PI quiescents.

## Tubes

| Part | Model | Parameters |
|------|-------|-----------|
| V1, V2 preamp; PI | `KorenTriode` | Koren ECC83/12AX7 default (mu 100, Ex 1.4, Kg1 1060, Kp 600, Kvb 300) |
| V2/V3 drivers | `KorenTriode` | Koren 12AU7 (mu 21.5, Ex 1.3, Kg1 1180, Kp 84, Kvb 300) |
| 6x 6550 (two triples) | `KorenPentode` | Koren 6550 (mu 7.9, Ex 1.35, Kg1 890/3, Kg2 4200/3, Kp 60, Kvb 24), `grid`, `lambda`, `arcVoltage` defaults |

## Controls

Page 1 mirrors the SVT faceplate: Input (0 dB / −15 dB jacks), Gain, Ultra-Lo, Ultra-Hi, Bass, Mid
(+3-position frequency select), Treble, Master. Page 2 is the project's synthetic page: Power Drive
(pre-PI pad), Bias (the level-shifter tap, hot ↔ cold), Tube Feel (supply sag + feedback amount),
Speaker (2/4/8 ohm) and Output (plug-in level — the real amp has no output-level control).

## Honest simplifications (documented, not hidden)

- The NE5532 input buffer + protection diodes are replaced by the standard `inputLimit()` soft clip;
  it is a unity buffer inside the amp's headroom.
- The PI is modelled as a long-tailed pair; the CL's actual V1B grid is a paraphase-style tap of V1:A's
  plate through a bias network. Same differential function, more robust for the solver; the small
  balance difference is folded into the driver level-shifter anyway.
- Ultra-Lo is modelled as the switched R20/R21/C8 mid-scoop divider shown on the preamp schematic;
  Ultra-Hi as the switched extra C21 .01 in the bass wiper leg. The exact switch-pin routing is partly
  illegible on the scan; the published responses (±20 dB at 8 kHz / 600 Hz mid scoop) bound the error.
- The mid trap uses (C,L) pairs per position — measured resonances: 800 mH+.68 µF ≈ 216 Hz,
  300 mH+.15 µF ≈ 752 Hz, 100 mH+.033 µF ≈ 2.77 kHz, against the published 220/800/3000 Hz.
- OT primary impedance is not published. ~600 ohm plate-to-plate is used: consistent with 300 W into
  4 ohm at ~660–690 V B+ (half-primary swing ≈ ±640 Vpk needs ≲ 700 ohm to deliver 300 W), and with the
  value clone builders quote for this transformer. NFB resistor value likewise unreadable on the scan —
  a modest loop (~8 dB closed-loop reduction) is fitted and marked as an estimate.
- The −180 V driver-return rail is a constant source (it is a lightly-loaded regulated winding).
- The level-shifter tap needs ~265k below it (not the ~186k a first reading of the component
  numbers suggested) for the grids to land at the marked −45 V given the modelled ~170–220 V driver
  plates; the Bias knob sweeps 180k–350k around that. 0.1 µF bypass caps sit on the taps, as the real
  bias network's electrolytics do -- without them Newton gets trapped in bursts when a 6550 bank swings
  into grid current (measured: 228 failed solves in a 1.7 s clean-tone run before adding them, 0 after).
- Bias-indicator/protection op-amps, per-cathode 10 ohm sense resistors, 1N3070 clamps: metering
  only, no signal-path effect — omitted.
- The OT secondary's winding sense in the coupled-inductor matrix is chosen so the global feedback
  returned to the second PI grid is NEGATIVE. Built the other way the 150k NFB path becomes positive
  feedback and the amp self-oscillates a ~9 Hz relaxation (motorboating) that buries the output —
  measured 2026-10-09 as 0.109 RMS of low rumble on a zero-signal input; caught by the
  `AmpAudibleOutput` silence regression test.
- The NFB loop is where this amp is fragile: with the sign right it still sustained a rail-to-rail
  relaxation (~2 Hz at the published values, ~0.25-4 Hz once disturbed) at Master = 0 under a driven
  input — audible as DC/rumble and a thump when the knob moves. Measured 2026-10-10: opening the
  150k return is the only single edit that silences it outright. The applied fix is two-part:
  (1) the PI LTP gets kg1 softened 24x (self-biased tail, so the DC points hold; the fixed-bias
  6550s cannot be softened without starving their ~0.3 A idle), cutting the loop's incremental
  gain below what re-arms the hop; (2) every high-pass pole inside the loop — C8/C11 and the NFB
  injection cap — is pushed far below the OT's own LF corner (2.2 uF each, electrolytic-sized),
  because stacked HP poles each add up to +90 deg of phase lead below their corner and flip the
  feedback positive right where the relaxation runs. Driver grids also get the same 47k stoppers
  the 6550s have, limiting grid-conduction charging of C8/C11. Residual: at Bias = max with a
  slammed input a ~-35 dBFS infrasonic rumble survives — physically real (a real SVT motorboats
  biased hot); every normal setting is silent.
- V2:A's grid leak R14 returns to the kx cathode-resistor tap, not ground (bootstrapped bias): the
  follower idles with its cathode ~100 V. Wired to ground it idles near 5 V on the grid-current knee.

## Verification targets

Schematic-marked DC points the model should land near (±20 %, same convention as the other amps):

- V1:B plate 235 V; V1:A plate 240 V; V2:B plate 240 V; V2:A cathode ~100 V.
- PI plates ~200 V (V1:A) / ~250 V (V1:B); PI cathode ~5.7 V; driver cathodes ~5.5 V.
- 6550 grids ≈ −45 V; screen rail 365 V; plate rail ≈ 680 V nominal.
- Tone: ±12 dB @ 4 kHz (treble), ±12 dB @ 40 Hz (bass), scoop centres 220/800/3000 Hz.
