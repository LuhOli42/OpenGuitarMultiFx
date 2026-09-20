# TS808 / TS9 / TS10-Style Overdrive

One processor (`TubeScreamerStyleOverdriveProcessor`), three registered
models, because the three pedals share one circuit. Display names are
**"TS808-Style Overdrive"**, **"TS9-Style Overdrive"**, **"TS10-Style
Overdrive"** — same trademark convention as
[`OD1StyleOverdrive.md`](./OD1StyleOverdrive.md) (never the bare
trademarked name, even though the circuit is thoroughly documented in the
open).

Family: [op-amp diode-in-feedback clippers](./CircuitFamilies.md) — same
mechanism as the BOSS OD-1 (the credited ancestor), but a **symmetric**
diode pair, plus a real passive+active tone stage the OD-1 doesn't have.

## Sources, and how they were cross-checked

- **R.G. Keen, "The Technology of the Tube Screamer" (Geofex, 1998)** —
  the primary source: component values, the block-by-block analysis, and
  the *only* source found that states exactly what differs between the
  TS808, TS9 and TS10.
- **A clean KiCad redraw of the TS808** (barbarach.com, itself adapted
  from ElectroSmash and Geofex) — read as an image for the exact wiring,
  especially the tone stage, which Geofex describes in prose only. The
  redraw substitutes convenient parts (BC549 for 2SC1815, NE5532 for
  JRC4558, 470K for 510K, 47pF for 51pF, 22nF for 20nF); **the original
  values from Geofex and the effectslayouts.blogspot.com BOM were used
  instead**, since those are what the real pedals carry.
- **A real TS-10 schematic** (Ivan Nuvoli, 12/12/97, hosted on
  experimentalistsanonymous.com, supplied by the project owner) — the only
  source found with actual TS10 values. It confirms all three differences
  Geofex lists *and* fills in the numbers Geofex omits. It is hand-drawn,
  so it was read at 2.5× zoom and cross-checked against Geofex. **One
  drafting error was identified and not followed:** it draws Q2's 510 K
  base-bias resistor to *ground*, which would leave the output transistor's
  base at 0 V (cut off) — contradicting Geofex ("biased from the 4.5 V
  source") and basic DC analysis; it is modelled to the 4.5 V rail like the
  TS808/TS9.
- ElectroSmash's own TS analysis page was unreachable from the dev
  machine when this was written; its schematic reached us only through
  the redraw above.

## What differs between the three models (and what this model does with it)

Per Geofex: **TS808 and TS9 (and the TS9 reissue) are the same board; the
ONLY differences are the op-amp type and two output-buffer resistors.**

| | R14 (series) | R15 (shunt) | Other differences modelled |
|---|---|---|---|
| TS808 | 100 Ω | 10 kΩ | — |
| TS9 | 470 Ω | 100 kΩ | — |
| TS10 | 470 Ω | 100 kΩ | see below |

The measured effect of the R14/R15 difference in this model is small
(~1.8% on the fundamental), matching Geofex's own arithmetic (0.990 vs
0.995 divider ratio) and his explanation that the 10 K shunt mostly changes
the follower's negative-going output impedance.

**The op-amp type is not modelled** (all models use an ideal op-amp, as
the OD-1 and DS-1 do). Geofex calls the op-amp "the single biggest effect
on the sound" of a real unit — JRC4558 vs. others recover from overload
differently. That is a real fidelity gap shared by every op-amp circuit
in this project, not something specific to this one.

### TS10 — modelled from the real schematic

Geofex lists three differences from the TS9; the TS-10 schematic gives
their real values and shows two more that are AC-invisible. All are
modelled (`ModelSpec` in the `.cpp`):

1. **Q1's bias voltage is 6.35 V, not 4.5 V.** The base resistor (510 K)
   returns to a node fed by 9.2 K from +9 V and 22 K to ground
   (9 × 22/31.2 = 6.35 V), decoupled by 10 µF. The emitter follower runs at
   more current (Q1 base 5.52 V vs 3.94 V for the TS9 in the model — matching
   a hand calculation).
2. **A 220 Ω between C2 and the op-amp's (+) bias node.** *This has a real
   (small) effect* — an earlier version of this document said an ideal
   op-amp makes it a no-op. That was wrong: the (+) pin's 10 K bias resistor
   sits on the **op-amp side** of the 220 Ω, so the two form a divider,
   10 K/(10 K + 220) = 0.978 (−0.19 dB), and the emitter follower sees a
   slightly different load. (Only a 220 Ω *after* the bias node would be
   invisible.)
3. **The extra emitter follower (Q6) feeds only the bypass JFET**, not the
   effect path — its only load on Q1 is ~1 K into a base with ~3 MΩ input
   impedance. Not modelled.
4. **The "bigger capacitor coming off the volume control" is real:** the
   Level wiper goes through an extra **1 µF** into a node with a **510 K**
   bias resistor to 4.5 V, then the closed JFET switch (~100 Ω), then a
   second node with another **510 K** to 4.5 V, then the 0.1 µF into Q2's
   base. These are the JFET's DC-bias resistors Geofex mentions. Their
   audible effect: the corners are far below 10 Hz, but the two 510 K
   resistors *load the wiper* — at Level 50% the wiper drives ~160 K
   instead of ~430 K, a ~0.6 dB drop (level only, not tone).
5. **DC-only differences:** op-amp 2's 10 K (R10) returns to +9 V instead
   of 4.5 V, and the Level pot's grounded end goes to 4.5 V. Both are AC
   grounds either way, so they don't change the signal — modelled for
   fidelity of the stored capacitor voltages.

Net result: the TS10 sits **~8% (−0.7 dB) below the TS9 in small-signal
level** (0.978 × ~0.934 × ~1.005 ≈ 0.918, hand-derived and measured —
see the test) with otherwise the same tone and clipping. Clipping level is
set by the diodes and is therefore essentially unchanged.

**Caveat carried by the TS808/TS9:** their bypass-JFET bias network is not
in the redraw or in Geofex's text, so they are modelled *without* the two
510 K loads. If the real boards have them (the JFET is biased the same way
by the same design), they would also sit slightly below the modelled level.
The redraw omits the switching network on purpose, so this is unverified
either way; it only shifts overall level, which the Level knob absorbs.

## Signal path

Designators follow the TS808 redraw (R1–R15, C1–C9).

### 1. Input buffer — Q1 (NPN emitter follower)

`EFFECT_IN → C1 (0.02 µF) → R1 (1 K) → Q1 base`, base biased by R2 (510 K)
to the 4.5 V rail; collector to +9 V, emitter to ground via R3 (10 K).
2SC1815-class, gain ~300 (Geofex). Always connected to the input, never
switched — which is why the pedal doesn't "tone-suck" the way wah pedals
do.

The emitter feeds C2 (1 µF) into op-amp 1's (+) pin, which has R5 (10 K)
to the 4.5 V rail **and nothing else** — an ideal op-amp draws no input
current, so C2 + R5 are simply a series load from the emitter to the rail.

### 2. Op-amp 1 — the clipper

Non-inverting. The virtual short puts the (−) pin at the (+) pin's
voltage `Vp`. **Input leg:** (−) → C3 (47 nF) + R4 (4.7 K) → ground. **Feedback:**
(−) ↔ output through **R6 (51 K) + Drive pot (500 K, "A" taper)** in
parallel with **C4 (51 pF)** and **D1/D2 (1N914/1N4148, one each way)**.

Gain (small signal) = `1 + Zf/Zi`. C3/R4 gives a 720 Hz corner, so bass
gets far less gain than the mids — the source of the pedal's "mid hump"
and its lack of muddiness. The test suite measures this directly:
gain 1.05 at 100 Hz vs 4.05 at 1 kHz (with Level at 50%, i.e. −12 dB, and
the tone stage's own filtering included).

**Solving it.** Because the op-amp forces `Vp` on the (−) pin, the current
through the input leg is *known*: `iF = (Vp − histC3)/(reqC3 + R4)`. It
must all return through the feedback network, whose voltage `v = Vout − Vp`
satisfies

    iF = v/Rf + (v − histC4)/reqC4 + Idiode(v)        (Rf = R6 + Drive)

Everything except the diodes collapses to a Thevenin equivalent
`(Rth, Vth)`, leaving `(Vth − v)/Rth = Idiode(v)` — one unknown, solved by
`AsymmetricDiodePair::solve` (1 diode each way = the symmetric case).
`Vout = Vp + v`. Same derivation pattern as the OD-1's op-amp 1, with C4
added to the feedback network as a trapezoidal companion model.

**Diode model:** standard SPICE 1N914/1N4148 — `Is = 2.52 nA`, emission
coefficient `N = 1.752`. The `N` matters: with `N = 1` a 1N4148 would turn
on near 0.33 V instead of the real ~0.6 V. (Series resistance `Rs =
0.568 Ω` is ignored.) **Note:** the OD-1 and DS-1 in this project use `Is = 2.52
nA` but *without* the `N = 1.752` factor — see "Open finding" below.

The Newton solve starts from an analytic upper bound on the root, not from
last sample's value; the warm-start version measured ~0.5% failed solves
at max Drive with a hot input (each an audible held-sample glitch); this
version measures 0.000000%.

### 3. Tone control and op-amp 2

`R7 (1 K)` from op-amp 1's output into node **A** = op-amp 2's (+) pin.
`C5 (0.22 µF)` A→ground (a 723 Hz lowpass with R7, Geofex), `R10 (10 K)`
A→4.5 V rail. The **20 K tone pot** ("B", linear) sits between A (pin 1)
and op-amp 2's (−) pin B (pin 3); its wiper feeds `R8 (220 Ω) + C6
(0.22 µF)` to ground. `R9 (1 K)` is op-amp 2's feedback resistor
(output → B).

Verified against the redraw *and* Geofex's prose: wiper toward A (pin 1) =
the shunt lowers treble (dark, Tone at minimum); wiper toward B = the
shunt is in the feedback path and treble is boosted back (bright).

**Solving it (closed form, linear).** Ideal op-amp ⇒ `V_B = V_A`. The
wiper node `W` is then linear in `V_A` (`V_W = α V_A + β`, from the
wiper's KCL against the R8+C6 shunt), which collapses node A's KCL to a
single linear equation. Op-amp 2's output is `V_A + R9·(V_A − V_W)/R_WB`
(it supplies the current the pot's B side draws through R9). Tests:
6 kHz output rises 6.5× from Tone 0 → 1 while 200 Hz moves only 1.16×.

### 4. Level, JFET switch, output buffer

`C7 (1 µF) + R11 (1 K)` into the top of the **100 K Level pot** ("A"
taper). The wiper goes through the (closed) bypass JFET — modelled as a
plain **100 Ω** resistor, its "on" resistance per Geofex — and `C8
(0.1 µF)` to Q2's base (R12 = 510 K to the rail). Q2 is an emitter
follower with R13 (10 K) to ground, then `R14 → C9 (10 µF) → R15` to
ground as the output. Downstream input impedance is assumed 1 MΩ (same
convention as the OD-1/DS-1).

## Pot tapers

Drive is A500K and Level is A100K (audio/log taper), Tone is B20K
(linear) — per the TS10 layout's own pot notes. The audio tapers are
approximated as `R = Rmax × knob²`, the same documented stand-in the
Rangemaster-style booster uses for its log pot (real log pots aren't pure
log curves either). For Drive the resistance is counted from the R6 end;
for Level the wiper-to-ground segment is `Rmax × knob²`.

## Not modelled

- **The bypass/footswitch network** (two JFETs, the flip-flop, the
  input-buffer→JFET 0.1 µF coupling, the gate RC soft-switching). This
  project's own `EffectProcessor` enable/disable replaces it. The bypass
  JFET's "off" leakage into the effect path is ignored.
- **Real op-amp behaviour:** finite gain-bandwidth (~3 MHz for a 4558;
  matters slightly at max Drive), slew rate, output rail clipping, and the
  op-amp-type differences Geofex describes. Ideal op-amp throughout.
- **The 4.5 V bias generator** (R16/R17 + C11) is treated as an ideal
  fixed rail: its 5 K source impedance is small against everything it
  feeds, and C11 makes it an AC ground far below audio.
- **Power-supply behaviour** (battery sag, the reverse-polarity diode).

## Open finding (not a TS bug)

While choosing the diode parameters here, the OD-1 and DS-1 processors
were found to use `Is = 2.52 nA` with `Vt = 25.85 mV` *without* the
1N4148's emission coefficient `N = 1.752`. That puts their diodes' turn-on
near 0.33 V rather than ~0.6 V — i.e. their clipping thresholds are
roughly half what the real diodes give. Not changed as part of this work
(both were signed off by ear), but worth a deliberate decision: the OD-1
would sound harder-clipped and brighter-edged with the corrected value.
Tracked on the project board.

## Verification (Tests/TubeScreamerStyleOverdriveProcessorTests.cpp)

Silence settles; bounded under a loud sine at every Drive setting on all
three models; the diode solve converges (failure rate ≈ 0 at max Drive,
hot input, 3 kHz); Q1 is forward-active (not cutoff); positive/negative
clipping peaks match to within 5% (measured ~0.3%); the mid-hump gain
ratio; Drive is a monotonic gain control; Tone moves treble far more than
bass; Level scales output; TS808 vs TS9 differ by less than 3% (measured
1.8%); TS10's Q1 bias and small-signal level trim match the hand derivation
(base 5.52 V vs 3.94 V; ratio 0.918); stereo channels stay identical.
