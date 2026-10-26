# Divided by 13 FTR 37 — physically modelled amplifier

## Source

There is no factory schematic. The model follows **Edwin Thoen's trace** of a real FTR 37, drawn 3-3-2008 and posted
on the Das Musikding forum as "Divided by 13 FTR 37 Schematic!". It is three hand-drawn pages (preamp; reverb +
phase inverter; power amp + supply) with the DC voltages he measured. A copy is also on Scribd. It is community work,
not factory, but it is a complete trace with measured voltages.

The earlier version of this model was built without it, from the tube complement alone. These parts of that version
were wrong:

- the channels mixed through 100k into one PI input;
- channel 1 was a "Click" shunt cap after a second gain stage;
- the PI was a grounded-tail LTP;
- the bias was pure fixed.

## What the trace shows, and what is modelled

### Channel 1 (`preCh1`)

- Input: 68k into the **5879** (1M leak).
  - 220k plate on B+1, marked 81 V; the model reads 88 V.
  - Screen through 1M + 100n: marked 50 V. That reading went through the 1M with a 20 kΩ/V meter and reads low, so
    the model uses 65 V.
  - Cathode: 2k2 + 25 µF.
- 100n → the **TONE rotary**. It selects the *series* coupling capacitor into the Volume: 250p, 500p, 2n, 4n7, 10n,
  22n. Clockwise passes more lows, as the manual says.
- **Volume** A1M with a 100p bright cap, into V2a: 100k plate, 1k cathode, unbypassed.
- V2b: a DC-coupled cathode follower (100k), then 1M + 22n into the PI's **first grid**.

### Channel 2 (`pre`)

- Input: 68k into V3A: 100k plate (marked 138 V), 1k5 cathode. Its 22 µF bypass is on the Volume's **pull** switch
  (Boost).
- The Treble / Bass stack straight off the plate: 250p, 100k slope, Treble A250k, Bass A250k, 100n / 47n, 6k8.
- **Volume** A1M from the Treble wiper, with 82p bright.
- V3B: 100k plate, 1k5 + 25 µF.
- 22n → 3M3 ∥ 10p into V5B (220k grid leak; 100k plate, 820 + 25 µF).
- 100n → 1M + 1n into the PI's **second grid**.
- The reverb send / return around V4 / V5A is not modelled: the reverb is not part of this model.

### Phase inverter and power (`power`)

- **Phase inverter:** V6, ECC83. The two 1M grid leaks return to a node 1k2 below the joined cathodes, with 47k to
  ground (an elevated-reference pair). The two channels drive its two grids, so they interact only there.
- **Couplings:** 100k plates on B+3, then 150n with 2n2 to ground.
- **Grid circuit:** 220k leaks into the BIAS node, then 2k7 stoppers.
- **Output tubes:** four 6V6s with **mixed bias**:
  - one tube per side sits on a fixed 270 Ω 5 W + 50 µF cathode;
  - the other tube per side sits on a second 270 Ω + 50 µF that the **Half / Full** switch disconnects, cutting those
    tubes off;
  - the fixed part comes from the BIAS supply (a 1N4007 off the HT winding, with a B25k trim). Its −14 V is
    *estimated*.
- **NFB:** none.
- **Supply:** GZ34, then B+5 393 V (OT centre tap), choke, B+4 385 V (screens, 470 Ω per tube), then a 10k chain to
  B+3 265 V (PI), B+2 212 V (preamp) and B+1 206 V (the 5879). The model uses these marked ratios.

### Not on the trace, so estimated

- the output transformer;
- the GZ34 / PT resistance;
- the fixed-bias voltage.

## Controls

Page 1:

| Param | Real control |
|---|---|
| Input | Ch 1 / Ch 2 / Both, the separate jacks |
| Ch1 Volume | Channel 1 Volume |
| Ch1 Tone | The six-position rotary |
| Ch2 Volume | Channel 2 Volume |
| Boost | The pull switch |
| Ch2 Treble / Ch2 Bass | Channel 2 tone stack |
| Power | Full / Half |

Page 2 (synthetic): Power Drive, Bias, Tube Feel, Speaker, Output.

## Verification

- **DC:** rails 393 / 388 V; 5879 plate 88 V; V3 plates 147 V; PI plates 215 V with the cathodes at 52 V; power
  plates 391 V; grids at −14 V.
- **Tests:** the suite passes (silence, audible output, every knob's direction, Half/Full, Bias).
- **Reduced stage:** level refitted (0.155 → 0.212). It tracks the netlist within 0.46 dB of level and 1.79 dB of band
  shape (bound 2 / 4).
- **Trim:** −8.90 dB, giving unity.
- **CPU:** 11 % of a core in the shipped reduced build. The preamp now has the real stage count: 5879, follower, V5B.
