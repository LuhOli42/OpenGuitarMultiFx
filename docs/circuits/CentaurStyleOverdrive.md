# Centaur-Style Overdrive

Display name **"Centaur-Style Overdrive"** (the pedal's own name, per the
trademark convention in `OD1StyleOverdrive.md`; "Klon" is the maker). Runs on
[`NodalCircuit`](./NodalCircuitSolver.md).

## Source
A clean EAGLE redraw of the Centaur schematic (coda-effects.com), read at 2.5x
zoom, cross-checked against ElectroSmash's analysis text (via their archive
mirror). Two things the drawing doesn't print, both **assumptions**: the Gain
pot's value (100 K dual gang, from clone BOMs; its taper approximated as
`knob^2`) and the germanium diode's model (1N34A: Is = 200 nA, N = 1.3, chosen so
the drop is ~0.35 V at ~10 mA as ElectroSmash quotes).

## Topology (what the netlist is)
Not a chain — three paths meet at an inverting summing amp's virtual ground:

1. **Input buffer** (TL072 follower): R1 10K, C1 0.1 uF, R2 1 M to 4.5 V. Its
   output is exactly the (+) node's voltage (a JFET op-amp draws no input
   current), so it is its own tiny block.
2. **Gain stage** (non-inverting, IC1_B): C3 0.1 uF, R6 10K || C5 68 nF into the
   (+) pin; Gain gang 1 divides that node against 4.5 V (pin 1 -> wiper) while its
   other segment plus R10 2K feeds the (-) leg (R11 15K || C7 82 nF); feedback R12
   422K || C8 390 pF. Then C9 -> R13 1K -> **antiparallel germanium diodes to
   ground** -> C10.
3. **Feed-forward network 1**: R7 1.5K + C16 1 uF low-pass (106 Hz) -> R19 15K.
4. **Feed-forward network 2**: R16 47K from the clipped node, plus branches
   (C11+R15, R17, C12+R18) referenced to the wiper of **Gain gang 2**, whose
   ends hang off B through R5 5.1K || C4 68 nF (with R8 1.5K and C6+R9 to 4.5 V).
5. **Summing amp** (IC2_A, inverting): R20 392K || C13 820 pF.
6. **Treble** (IC2_B, inverting active shelf): R21 1.8K + R23 4.7K around a 10K
   pot, R22 = R24 = 100K, C14 3.9 nF.
7. **Output**: C15 -> R25 -> 10K Volume, plus the **clean bleed** that runs past
   the whole effect (B -> C2 -> R4 -> R26 68K -> output).

Crossing wires were checked against dots, as with every schematic here. The
diodes go to **ground, not 4.5 V** (verified in a 6x zoom); the LED/switch and the
bypass are not modelled.

## Verification (Tests/CentaurStyleOverdriveProcessorTests.cpp)
- Gain stage at max Gain, 1 kHz: **36.2 dB**. The ideal formula for the stage
  alone gives 40.05 dB (1 + Zf/Zi, hand-computed); the 4 dB difference is the
  input network (C3 against R7+C16 and R6||C5 attenuates ~6 dB before the
  op-amp). ElectroSmash's "~40 dB near 1 kHz" is the stage figure.
- Mid hump: gain 3.8x @100 Hz, 64x @1 kHz, 17x @10 kHz.
- Treble: 15.8x more 8 kHz between the two ends (formula Gvmax/Gvmin =
  (10K+4.7K)/1.8K over 4.7K/(10K+1.8K) = 16.7x).
- 3rd-harmonic distortion vs Gain: 0.01% / 0.36% / 3.2% / 17%; clipping peaks
  symmetric (2.2766 / -2.2766). The second gang **balances level against drive**,
  so output level is *not* monotonic in Gain (0.54, 0.44, 0.67, 1.05 V) — that is
  the pedal's design, not a bug.
- Cost 1.1 us/sample (5% of a core, mono).

## Not modelled
Op-amp rail clipping (TL072 on 9 V and, after the diodes, on the 18 V/-9 V
supply — the charge pump only sets headroom, and with ideal op-amps there is
none to model; ElectroSmash notes the gain stage does hit its rails at high Gain,
so extreme settings will be softer here than on the real pedal), the LED, the
bypass switching, the anti-pop network's contribution when the pedal is off.
