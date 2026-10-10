# MuTronIIIStyleFilter — Mu-Tron III Envelope Filter

## What the real unit is

The Mu-Tron III (Musitronics, 1972) is the canonical envelope-followed filter — the
circuit the MXR M82 Bass Envelope Filter also derives from. The Geofex "Neutron"
workalike documents the topology openly:

1. An **inverting input amplifier** whose feedback is set by the Gain pot (≈ 40× down to
   ⅛) — the one knob sets both sensitivity and drive.
2. A **three-op-amp state-variable filter**: an inverting summer followed by two
   inverting integrators (C6 = C8 = 1.8 nF; the Low range switch adds 2.2 nF in parallel
   to each), producing simultaneous HP/BP/LP outputs. Positive feedback of the LP output
   into the summer — the **Peak** control — reduces the damping and creates the resonant
   quack.
3. A **precision rectifier + RC envelope detector** (C9 = 4.7 µF: ~1 ms charge through
   the diode's forward resistance, ~150 ms discharge through the bleeder).
4. A **lamp/LED + photocell optocoupler**: the envelope drives the emitter; the CdS LDR,
   wired in parallel with the integrators' sweep resistors, pulls the filter's centre
   frequency up as the note's envelope rises. The CdS cell is itself slow and asymmetric
   (~15 ms attack, ~250 ms decay), which is the pedal's signature sweep feel. The Drive
   switch routes the optocoupler for an up or down sweep; the Range switch picks the
   integrator capacitors.

Front panel: **Gain, Peak, Mode (LP/BP/HP), Range (Lo/Hi), Drive (Up/Down)**.

## What is modelled

The SVF is solved in **closed form** — the exact trapezoidal discretisation of the real
summer + integrators, back-substituted so no iteration is needed:

```
hp' = −(vin + lp' + d·bp')        (inverting summer, d = damping)
bp' = bp − a(hp' + hp);  lp' = lp − a(bp' + bp),   a = Ts/(2·R·C)
```

Substituting the integrator updates into the summer gives `hp'` directly
(`a` changes every sample as the LDR sweeps R). The summer's op-amp is given a soft
±6 V rail (tanh) — the real op-amps on dual 9 V batteries saturate there first.

- **Detector**: |v1| peak-followed with the published 4.7 µF attack/release law, then a
  second asymmetric one-pole for the CdS cell's own response.
- **Optocoupler law**: the LDR sits in parallel with a 470K sweep resistor;
  Rldr spans ~18K (lit) to ~10M (dark). Up-sweep: Reff = 470K‖Rldr — a ~200 Hz–5 kHz
  sweep in Hi, ~90 Hz–2.3 kHz in Lo. Down-sweep mirrors the control law (see below).
- **Peak**: damping 1.1 → 0.13 across the knob (Q ≈ 0.9 → ≈ 7.7).
- **Gain**: the input amp's real −8 dB…+32 dB range, log taper.

## Deliberate simplifications

- The Drive/Down switch: the real unit re-routes the optocoupler to invert the
  envelope's effect on the sweep; this is modelled as a mirror of the same
  resistance-vs-light law (`v → 1−v`), which is functionally identical.
- Component values the published schematic leaves ambiguous (LED bias, threshold) are
  chosen to place the quiescent filter at the bottom of the sweep with no signal, as on
  the real pedal.
- Only the summer's saturation is modelled; the two integrator op-amps stay linear
  (they operate well inside the rails in the real unit too).
