# ODB3StyleOverdrive — Boss ODB-3 Bass Overdrive

## What the real unit is

The Boss ODB-3 is the bass member of Boss's op-amp/FET overdrive family (same generation
as the OD-3/BD-2/FZ-2 circuit family). Published schematic walkthroughs (the indyguitarist
scan and the sdiy analysis) describe:

1. A high-impedance input buffer,
2. an op-amp gain stage with diode limiting and the **Gain** control in its feedback,
3. further nonlinear (FET-based) gain stages buffered by a PNP,
4. a pair of **LED clippers** shunting the signal path (the ODB-3's characteristically
   harder, higher-threshold clip — LED Vf ≈ 1.7 V versus ~0.6 V silicon),
5. a **Balance** blend that mixes the driven signal against a clean low-frequency feed —
   the reason the pedal keeps a solid bottom end,
6. a Low/High tone section (the low band is built around an NPN transistor used as a
   virtual inductor/gyrator) and **Level**.

Front panel, in order: **Level, High, Low, Balance, Gain**.

## What is modelled

Thevenin-chain + Newton solves per sample, same technique as the TS9B model:

- **Input buffer** — ideal follower (Vb − 0.62), emitter feeding both the clipper's (+)
  bias node and the clean path.
- **Gain stage** — a non-inverting op-amp with R4 4.7K + C3 47 nF to the rail and
  R6 51K + A500K Gain pot with the symmetric 1N914-class pair and C4 51 pF in feedback
  (1D Newton) — the family's shared clipper cell.
- **LED clipper** — the op-amp output through 4.7K into a node shunted to the bias rail
  by an LED pair (Is = 6e-12, nVt = 0.09 reproduces the ~1.7 V knee at ~1 mA), solved by
  the same `AsymmetricDiodePair` Newton with the diodes' port referenced to the rail.
- **Clean low path** — the buffered input through a one-pole RC low-pass (~480 Hz corner)
  before the blend, so the bass fundamental stays uncompressed, matching the pedal's
  documented behavior.
- **Balance** — a resistive summer crossfading the LED-clipped signal against the
  low-passed clean feed.
- **Low/High tone** — the bridged-feedback Baxandall arrangement (one pot per band
  spanning the op-amp's two virtual-short inputs, each wiper driving its own RC shunt —
  the same structure the TS9B model uses, here standing in for the gyrator low band
  plus high shelf of the real unit).
- **Level + output** — the Boss family's output network: coupling cap + 1K into the
  A100K Level pot, closed switch, C8, biased follower, 470 Ω + 10 µF + 100K.

## Deliberate simplifications

- The published ODB-3 schematic is only partly legible, so intermediate-stage values are
  estimated on the documented topology (the nonlinear JFET pairs are folded into the
  op-amp clipper's gain — the audible result is the same cascaded soft-clip-then-LED-clip
  curve; flagged as an estimate, not a claimed BOM).
- The gyrator low band is realised as the Baxandall low shelf — same boost/cut function
  at the same band region.
- The clean path's exact corner is estimated at ~480 Hz (published descriptions only say
  it preserves the low end).
