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

**Non-inverting input (pin 5)**: R6\* (4.7K) + C3 (0.047uF) in series to
BIAS1, with *nothing else* attached to this pin anywhere in the circuit.
Since an ideal op-amp draws zero input current and this is the pin's
*only* connection, zero current ever flows through R6\*/C3 — which means,
provably (not assumed), **pin 5 sits at exactly BIAS1, permanently**;
R6\*/C3 have no effect on the signal and aren't modelled as dynamic
elements at all. (\*Renamed `r6OpAmpBias` in code — the schematic reuses
"R6" for both this and the unrelated 33K BIAS1-divider resistor; real
designators, disambiguated here to avoid a C++ identifier clash, not a
schematic error.)

**Inverting input (pin 6)**: held at the same voltage as pin 5 (BIAS1) by
the op-amp's own negative feedback (ideal virtual-short) — same "pinned
voltage, current still flows and does the real work" behaviour any
inverting op-amp stage has. Fed by C2 (from Q6's emitter) with R4 (100K)
providing the bias-return path to BIAS1.

**Feedback network**: R5 (33K) in series with **VR1 (Drive, 1MB linear)**
wired as a variable resistor (wiper + one end, matching how this pedal
family typically wires a "gain" pot rather than as a 3-terminal divider),
forming a 33K–1.033M total feedback resistance from pin 6 to the output
(pin 7). **D5 (1 diode) in parallel with D6+D7 (2 diodes in series, same
direction as each other, opposite direction to D5)** bridge directly
across this same feedback resistance — the textbook "diode(s) in the
feedback loop" clipping topology, and the specific asymmetric (1-vs-2)
diode count is what gives the OD-1 its documented "smoother, warmer, less
edgy" asymmetric clipping character (one polarity clips at approximately
one silicon diode drop, ~0.6V; the other at approximately two, ~1.2V).

**Why this needs a genuine 1D Newton-Raphson solve, unlike the DS-1's
closed-form op-amp stage**: the DS-1's op-amp gain stage had a *linear*
feedback network (a plain resistor divider), so its closed-loop gain has
an algebraic closed form. Here the feedback element itself is nonlinear
(the diode pair), so the closed-loop operating point has to be solved
iteratively — same category of problem as a transistor's KCL, just one
unknown instead of three. See `AsymmetricDiodePair.h` for why `chowdsp_wdf`'s
own `DiodePairT` doesn't cover this (it only supports the *symmetric*
same-diode-count-both-ways case) and why this is new, generically reusable
infrastructure rather than a one-off hack.

**The exact equation solved every sample**: with pin 6 pinned at BIAS1
(call it `v5`), the current flowing INTO pin 6 from the C2/R4 input
branches is fully determined (`Iin = (Vth_in - v5) / Rth_in`, from the
usual parallel-Thevenin combine of those two branches). Since the op-amp
itself draws none of that current, it must all flow back out through the
feedback network — call `D = v5 - V(pin7)` the voltage across that
feedback network (linear resistor parallel with the diode pair). Then
`Iin = D/Rfb + I_diode(D)`, i.e. exactly `AsymmetricDiodePair::solve()`'s
own residual equation with `rth = Rfb` and `vth = Iin * Rfb` — reused
directly, unmodified, rather than re-derived. `V(pin7) = v5 - D` once
solved.

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
