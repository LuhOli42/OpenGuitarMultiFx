# OD-1-Style Overdrive

Modelled from the BOSS OD-1's factory board schematic ("ET-23D", sourced
via hobby-hour.com's clean scan — the user's originally-linked
experimentalistsanonymous.com image was, like the DS-1's, a simplified
redraw missing the input/output buffer transistors and the true-bypass
switching, though its core 2-op-amp topology and component values matched
closely enough to help confirm the trace). Cross-checked against multiple
independent OD-1 write-ups (ElectroSmash-style community analysis via
search, freestompboxes.org, Aion FX's Corona/OD-1 documentation), which
converge on the one fact this pedal is historically known for: **3
silicon diodes in the first op-amp's negative feedback loop, 2 in series
one way and 1 the other — genuinely asymmetric clipping**, and the widely
credited ancestor of the Ibanez Tube Screamer's near-identical "diode in
op-amp feedback" topology (TS-808, 1979, two years after the OD-1).

Display name is **"OD-1-Style Overdrive"** — same trademark convention as
[`PositiveGroundBooster.md`](./PositiveGroundBooster.md) and
[`DS1StyleDistortion.md`](./DS1StyleDistortion.md).

Unlike the DS-1, the OD-1 genuinely has only two controls — **Drive** and
**Level** — confirmed both by this schematic (only VR1 and VR2 exist) and
by every historical source describing the pedal (the Tone knob was a
later SD-1/DS-1 addition). Don't add a third knob this real pedal never had.

## Not modelled: the footswitch/LED sub-circuit

Same call as the DS-1: the true-bypass FET-switch network (Q2/Q3/Q4/Q5,
D9-D11, the flip-flop, the "CHECK" LED driver, R18-R29, C10-C16, SW) is
ignored — this project's own `EffectProcessor` enable/disable already
replaces it.

## Signal path

### 1. Input buffer — Q6 (NPN)

Plain emitter follower, structurally identical to the DS-1's Q1/Q3: base
via R1 (1K, folded into source impedance) + C1 (0.047uF) from the input,
collector tied directly to V+, emitter via R3 (10K) to true ground,
AC-coupled onward via C2 (0.0047uF). Base bias via R2 (470K) to **BIAS1**
(R6=33K from V+ / R7=33K+R30=470 to ground, filtered by C9=47uF — the
same single-supply virtual-ground construction the DS-1 uses, treated as
an ideal fixed `supplyVoltage/2` rail for the same reasons documented
there: the divider's own resistors are far smaller than everything
loading it, and C9's bypass makes it a true AC ground at audio frequencies).

### 2. Op-amp 1 — the drive/clipping stage (the pedal's whole reason to exist)

> **Corrected 2026-09-20 -- this stage was wired as an INVERTING amplifier and it is not.**
> The earlier reading of the scan put the signal (C2 + R4) on the inverting pin and the
> R6 4.7K + C3 47 nF branch on the non-inverting pin as an isolated network. The real
> OD-1 (Aion FX's Corona documentation, a clone of the original quad-op-amp circuit, shows it
> unambiguously; every write-up of the OD-1/SD-1/TS family says the same) is a
> **non-inverting** stage: the buffered signal enters the (+) pin through C2 with R4 biasing
> it to BIAS1, and **R6 (4.7K) + C3 (0.047 uF) is the gain leg from the (-) pin to BIAS1**.
> With the wrong wiring the stage's gain was -(R5 + Drive) / (C2 at 1 kHz ~ 34K) -- 0 dB at
> Drive min and 30 dB at max, 15 dB short of the real ~45 dB.

**Non-inverting input (+)**: fed by C2 (0.0047 uF, from Q6's emitter; the follower's 74 ohm
output resistance in series) with R4 (100K) to BIAS1. It draws no current, so its voltage is
BIAS1 + i(C2) * R4; C2/R4 form a 340 Hz high-pass.

**Inverting input (-)**: held at the (+) voltage by the op-amp. The **gain leg** R6 (4.7K)
+ C3 (0.047 uF) to BIAS1 draws a current from it, and all of that current must come from the
output through the feedback network: gain = 1 + Zfeedback / Zleg. That is 1 + 33K / 4.7K =
8 (18 dB) at Drive min and 1 + 1.033M / 4.7K = 220 (47 dB) at max at high frequency, with the
47 nF against 4.7K putting the gain's own high-pass corner at 720 Hz (at 1 kHz: 16.5 dB and
45 dB). Measured: 15.8 dB and 43.8 dB at the op-amp output.

**Feedback network**: R5 (33K) in series with **VR1 (Drive, 1M; Corona's parts list says
1MA -- audio taper, approximated as knob^2, like the TS and BD-2 pots; the earlier "1MB
linear" reading was from the scan)**, from the (-) pin to the output. The diodes bridge
this feedback network -- the textbook "diodes in the feedback loop" topology. **Two diodes
clip the positive peak (~1.2 V), one the negative (~0.6 V)** (the SD-1/OD-1 arrangement):
the asymmetry that gives the OD-1 its "smoother, warmer, less edgy" character.

**The equation solved every sample** (1D Newton-Raphson, `AsymmetricDiodePair`): the current
through the gain leg is fully determined by the (+) voltage; it must flow through the
feedback network, `Ileg = D/Rfb + I_diode(D)` with `D = V(out) - V(-)`, i.e.
`AsymmetricDiodePair::solve()` with `rth = Rfb`, `vth = Ileg * Rfb`; `V(out) = V(+) + D`.
Same structure as the Tube Screamer's stage.

**Diode orientation** (which half-cycle clips at ~0.6V vs ~1.2V) is a
flagged interpretation, not a pixel-certain read of the scanned image's
tiny diode-arrow glyphs — same category of judgment call as the DS-1's
Tone-stack lug assignment. Getting this backwards would swap which
half-cycle clips harder without changing the fundamental asymmetric
character, so it's a low-risk place to accept the ambiguity.

Diode parameters reuse the same representative small-signal-silicon
values as the DS-1's D4/D5 (`Is` ≈ 2.52e-9, `Vt` = 25.85mV) — same
"documented, adjustable assumption" status, scaled by diode count (1 vs 2)
via `AsymmetricDiodePair`'s own `diodeCountForward`/`diodeCountReverse`
parameters (the standard "N series diodes ≈ one diode at N×Vt"
approximation, the same technique `chowdsp_wdf`'s own `DiodePairT` uses
via its `nDiodes` parameter).

### 3. Op-amp 2 — fixed-gain buffer with a treble-cut

Plain, fully linear inverting stage: R7 (10K) from op-amp 1's output to
pin 2 (inverting input); R8 (10K) parallel with C4 (0.018uF) as feedback
back to pin 1 (output); pin 3 (non-inverting) is, by the exact same
"nothing else attached, zero current, so it's just fixed at BIAS1"
argument as op-amp 1's pin 5, pinned at BIAS1 via R9 (10K). Closed-loop
gain is a plain `-R8/R7 = -1` (unity, inverting) at DC, rolling off above
`1/(2*pi*R8*C4) ≈ 884Hz` — a treble-taming stage, not a gain stage. Fully
closed-form (linear feedback, no Newton-Raphson needed), same reasoning
as the DS-1's non-inverting op-amp stage.

### 4. Level control and output buffer

Op-amp 2's output couples via C5 (1uF) + R10 (4.7K) into **VR2 (Level,
10KB linear)** wired as a simple 3-terminal bleed-to-ground volume
control (top from R10, bottom to BIAS1 — not true ground, per this
schematic's own bus trace), no Tone stack in between (the OD-1 doesn't
have one). The wiper feeds Q1 (JFET) — modelled as a fixed near-zero
closed-switch resistance, same reasoning as the DS-1's Q7: it's
positioned exactly where a "pass the wet signal" bypass switch would sit,
and this project's own bypass already replaces that function — then C7
(0.047uF) into Q7 (NPN), the real output buffer (emitter follower,
mirrors Q6's input buffer exactly): R12 (1M) base bias to BIAS1, R13
(10K) emitter to true ground, C8 (1uF) + R15 (100K, assumed downstream
load) to the output jack.

This whole stage is a linear tree (no bridging, unlike the DS-1's Tone
stack) — solved via the same nested-Thevenin-then-forward-substitute
ladder technique the DS-1's Level/output section uses, just without a
second (Tone) branch to reconcile.

## Component values

| Ref | Value | Role |
|---|---|---|
| R1 | 1K | input series (folded into source impedance) |
| C1 | 0.047uF | input DC block |
| R2 | 470K | Q6 base bias, to BIAS1 |
| R3 | 10K | Q6 emitter resistor |
| C2 | 0.0047uF | Q6 emitter -> op-amp 1 pin 6 coupling |
| R4 | 100K | op-amp 1 pin 6 bias, to BIAS1 |
| r6OpAmpBias | 4.7K | with C3, op-amp 1 pin 5 -- proven inert, not modelled dynamically (see above) |
| C3 | 0.047uF | ditto |
| R5 | 33K | op-amp 1 feedback, fixed part |
| VR1 (Drive) | 1MB linear | op-amp 1 feedback, variable part |
| D5 | 1 diode | feedback clipper, one direction |
| D6, D7 | 2 diodes in series | feedback clipper, other direction |
| R7 | 10K | op-amp 1 output -> op-amp 2 pin 2 |
| R8 | 10K | op-amp 2 feedback, with C4 |
| C4 | 0.018uF | op-amp 2 feedback treble-cut |
| R9 | 10K | op-amp 2 pin 3 bias, to BIAS1 -- proven inert, same as r6OpAmpBias |
| C5 | 1uF | op-amp 2 output coupling |
| R10 | 4.7K | Level pot input series |
| VR2 (Level) | 10KB linear | output volume, bleeds to BIAS1 |
| C7 | 0.047uF | Q7 base coupling |
| R12 | 1M | Q7 base bias, to BIAS1 |
| R13 | 10K | Q7 emitter resistor |
| C8 | 1uF | output coupling |
| R15 | 100K | assumed downstream load |
| supply | 9V battery | -> BIAS1 = supply/2 |

Transistor parameters (Q6, Q7): same representative small-signal silicon
NPN values as the DS-1's Q1/Q2/Q3. Diode parameters: same representative
small-signal silicon values as the DS-1's D4/D5, scaled by series count.

## Reused building blocks

- `EbersMollBJT` — Q6, Q7 (2 independent instances, both plain emitter
  followers, no cross-terminal feedback bridge needed — simpler than the
  DS-1's Q2).
- `TrapezoidalCapacitor` — every coupling/bypass cap.
- `AsymmetricDiodePair` (new this pedal) — the op-amp 1 feedback clipper.
  See that header for why `chowdsp_wdf`'s `DiodePairT` doesn't cover the
  asymmetric (1-vs-2 diode) case and why this is new, generically reusable
  infrastructure, not a one-off.

No `chowdsp_wdf` WDF-tree usage here (unlike the DS-1's diode clipper) —
`AsymmetricDiodePair` is a plain 1D Newton-Raphson solve in the same style
as `EbersMollBJT`/`ShichmanHodgesJFET`, direct nodal analysis throughout,
consistent with every other circuit in this project's "single/few
nonlinear device, no separate complex sub-network" category.

See [`CircuitFamilies.md`](./CircuitFamilies.md) for how this relates to
the DS-1 and the booster.

## Diode model (2026-09-20 fix)
`AsymmetricDiodePair` is given nVt = N x Vt with N = 1.752 (a real 1N4148/1S1588 with
Is = 2.52 nA). It used to get N = 1, which clipped at ~0.33 V (and ~0.66 V for the pair)
instead of the ~0.6 V / ~1.2 V this document describes, with a knee twice as sharp.
