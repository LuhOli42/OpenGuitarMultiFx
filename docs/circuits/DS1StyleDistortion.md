# DS-1-Style Distortion

Modelled from the BOSS DS-1's original (pre-1994, TA7136P-based) factory
board schematic ("DS-1 BOARD ASSY 7520551007", sourced via
hobby-hour.com's clean scan — the user's originally-linked
experimentalistsanonymous.com image turned out to be a simplified,
inaccurate single-op-amp redraw, not the real circuit; the user explicitly
chose to build the real one when asked). Cross-checked stage-by-stage
against ElectroSmash's independent DS-1 analysis (a later NJM2904L-based
revision — different reference designators, same topology and, where
components carry the same name in both revisions, identical values, e.g.
R13=4.7K in both), which supplied the one fact the scanned image alone
couldn't settle: the op-amp stage's exact feedback topology (see below).

Display name is **"DS-1-Style Distortion"** — never the bare "DS-1"/"Boss",
same trademark convention as [`PositiveGroundBooster.md`](./PositiveGroundBooster.md).

## Not modelled: the footswitch/LED sub-circuit

The lower third of the factory schematic (Q4/Q5/Q7... wait, see note below
on Q7, Q8, D6/D7, the 6-transistor cross-coupled flip-flop, the LED driver)
implements BOSS's silent FET-switched true-bypass + LED indicator. This
project's own `EffectProcessor` enable/disable already handles bypass at
the host level — reimplementing a physical switch's own click-suppression
circuitry would be modelling a problem this codebase doesn't have. Ignored
entirely, same call as `PositiveGroundBooster.md` made for that pedal's
jack-insertion battery switch.

## Signal path (left to right)

### 1. Input buffer — Q1 (NPN, 2SC2240GR/2SC3378GR)

Plain emitter follower (common collector): base driven from the input via
R1 (1K, series, folded into the source impedance) + C1 (0.047uF, DC
block), collector tied directly to the (assumed ideal, low-impedance)
V+ rail, emitter to ground via R3 (10K) with the output taken across R3,
AC-coupled onward via C2 (0.047uF... actually 0.47uF/50, see component
table). Base DC bias comes from R2 (470K) to **BIAS1** (see below), not
straight to ground or V+.

**BIAS1**: R24 (10K, from V+) / R25 (10K, to ground) form a divider at
V+/2, heavily bypassed by C15 (47uF) right at its source. This is the
circuit's single-supply "virtual ground" reference — every stage's inputs
bias to this node rather than to true 0V, giving every op-amp/transistor
stage symmetric headroom off one 9V battery. Modelled as a fixed DC
voltage source at `supplyVoltage/2`, not as its own dynamic node — R24/R25
are far smaller than everything that loads BIAS1 (R2=470K, R4=100K,
R5=1M, R11=100K...), and C15's 47uF bypass makes it a true low-impedance
AC ground at audio frequencies, so treating it as an ideal fixed rail is
an accurate, standard simplification, not a fidelity compromise.

Reused: `EbersMollBJT` (independent per-terminal Thevenin — collector is a
fixed source, base and emitter each have simple local RC networks, no
cross-coupling between Q1's own terminals).

### 2. Q6 — JFET used as a voltage-controlled resistor, not a gain stage

This is the one genuinely unusual stage, and worth explaining because it's
not what "JFET stage" usually means in a gain-stage writeup. Q6's **gate**
wires straight to BIAS1 (no series resistor in the gate line itself) —
since a JFET's gate draws ~0 current in normal bias, this pins
`Vgate == BIAS1` exactly, with no AC signal reaching the gate at all.
Q6's **channel** (drain/source, symmetric) sits directly in the signal
path: one side from Q1's emitter (via C2, with R4=100K to BIAS1), the
other side onward to Q2's base (via C3, with R5=1M to BIAS1, then C3
couples to Q2). Because gate, and (via R4/R5) both channel terminals, all
sit near BIAS1 at DC, **Vgs(DC) ≈ 0** — the device is biased near its
*most conductive* point (`Idss`), deep in the ohmic/triode region for any
audio-level Vds. It's used as a fixed(ish), input-signal-dependent
series resistor, not a transconductance amplifier: BOSS's designers get a
specific, well-controlled `R_DS(on)` from an off-the-shelf small-signal
JFET instead of a plain resistor, and — the fidelity-relevant part — the
JFET channel's real I-V curve is *not* perfectly linear even in triode,
so this stage contributes a small amount of level-dependent soft
saturation before the signal ever reaches the main gain stage. A plain
resistor here would silently discard that.

Modelled with the new `ShichmanHodgesJFET` (see that header for why a
2-unknown solve, not 3, is correct and sufficient here — gate current is
identically zero in this model, matching the real device's dominant
behavior at audio signal levels).

### 3. Q2 — common-emitter gain stage with shunt feedback

Base from Q6's channel via C3 (0.047uF); R6 (100K) base-to-ground;
R9 (22 ohm) emitter-to-ground (deliberately tiny — this is most of this
stage's gain); R8 (10K) collector-to-V+; **R7 (470K) parallel with C4
(250pF) directly bridging collector back to base** — classic shunt-
feedback self-bias, and also a treble-cut (C4 increasingly shorts the
feedback path at high frequency, taming fizz before the harder clipping
stages). Collector couples onward via C5 (0.47uF) to the op-amp stage.

**Simplification, explained (the kind the project's fidelity-priority
rule asks to flag rather than silently make):** R7/C4 connects two of
Q2's *own* three terminals directly to each other, which `EbersMollBJT`'s
independent-per-terminal-Thevenin interface can't represent exactly (it
assumes base/emitter/collector each see their own local network, not each
other). Two options existed: (a) generalize `EbersMollBJT` to accept a
full cross-coupled conductance matrix, a real architecture change touching
already-verified code under time pressure, for one feedback branch; or
(b) evaluate R7/C4's contribution to each terminal's Thevenin equivalent
using the *other* terminal's previous-sample voltage (a one-sample-delayed
feedback tap), keeping `EbersMollBJT` completely untouched. Went with (b).
This is a real, named technique in real-time nonlinear circuit modelling,
not an arbitrary shortcut, and its error is bounded and small here
specifically because R7=470K is large (this feedback branch carries a
small fraction of the stage's total bias current — R6/R8/R9 dominate the
DC operating point) and R7/C4's own corner frequency (~1.35kHz) is far
below the point where a one-sample delay (20.8us at 48kHz) becomes
significant. If a future circuit needs *exact* (non-delayed) multi-
terminal feedback on a single nonlinear device, extending
`EbersMollBJT` to a general conductance-matrix form is the right next
step — noted here rather than done speculatively.

### 4. Op-amp gain stage — IC (TA7136P, treated as an ideal op-amp)

**Non-inverting input (+)**: the actual signal input, via C5 from Q2's
collector, with R11 (100K) biasing it to BIAS1 (C5+R11 form a highpass
well below the audio band).

**Inverting input (-)**: the feedback/gain-setting node — held at
`V(-) == V(+)` by the op-amp's own negative feedback (ideal virtual-
short). R13 (4.7K) + C8 (1uF) run from it to **true ground** (not
BIAS1 — confirmed by the schematic's explicit ground tick here, unlike
almost everything else in this circuit). The **DIST pot (VR1, 100KB)**
sits in the feedback path from the op-amp's output back to this node, fed
via R14 (2.2K) in series from the output.

(The scanned TA7136P schematic prints "2"/"3" at the two input lines, and
R10/R11 both appear near that pair — which physical pin carries which
schematic-printed number wasn't fully pixel-certain, and TA7136P doesn't
share the 741's familiar pinout to cross-check against. What *is* settled,
independently, by ElectroSmash's verified analysis of the equivalent
NJM2904L-based revision: which role is which — signal in on the non-
inverting input, DIST-pot-plus-R13 feedback on the inverting input, exact
same formula. The implementation follows the functional roles, not a
specific pin-number label.)

**Why this reads as a plain non-inverting amplifier despite VR1 having 3
lugs, not 2:** VR1's wiper ties to BIAS1 (AC ground). A pot's wiper
doesn't break its resistive track — current can and does flow lug-to-lug
straight through it — so, electrically, this is exactly a classic non-
inverting-amp feedback divider (`Rf` from output to pin 2, `Rg` from pin 2
to true ground) where `Rf` is VR1's own end-to-end resistance and the
wiper-to-BIAS1 tap just fixes *where along that resistance* the "pin 2"
connection effectively sits — it changes the pot's *taper feel*, not the
gain formula. This matches ElectroSmash's independently-verified,
explicitly-quoted formula for the equivalent stage in their (differently-
numbered but topologically identical) revision:

```
Gain = 1 + Rf/Rg = 1 + VR1/R13 = 1 + 100K/4.7K ≈ 22.3x (26.5 dB) at full Drive
```

**Why this is solved in closed form, not Newton-Raphson:** the op-amp
(pin 2 held exactly at pin 3's voltage regardless of downstream loading,
zero output impedance) makes the node between R14 and the DIST-pot/C9
(call it Node MID — the effective "clipping-stage drive" node) a rigid,
zero-impedance voltage source as seen by everything after it: whatever
current the diode clipper or C9 demand, the op-amp supplies it through
R14 without perturbing the feedback network's own operating point, because
the feedback loop's job is specifically to keep pin 2 == pin 3 no matter
what. Working the KCL through by hand gives a direct formula (see
`.cpp`) — `V(pin6)` itself is never needed by anything downstream, so it's
not computed at all. This is the "ideal op-amp" simplification, and it's
flagged explicitly here per the project's own rule about explaining
tradeoffs — but it is categorically different from the kind of shortcut
rejected for the transistors: a real op-amp's open-loop gain (>10,000x,
often >100dB) genuinely makes the ideal/virtual-short model accurate to a
fraction of a percent within the audio band, whereas approximating a BJT
as non-loading discards a big, audible part of what makes it sound like
that BJT.

**Diode clipper**: Node MID (through C9, 0.47uF NP) feeds Node CLIP, shunt-
clipped by D4/D5 wired back-to-back (anti-parallel) to BIAS1 — classic
Distortion+-style hard clipping to AC ground, confirmed by both the
schematic and Aion FX's description ("similar to circuits like the
Distortion+"). Modelled with `chowdsp_wdf`'s own `DiodePairT` (Werner et
al.'s Lambert-W closed-form solve) fed by a `ResistiveVoltageSourceT`
built from Node MID's Thevenin-through-C9 equivalent — the one place in
this processor that's a genuine (if minimal, single-adaptor) WDF tree,
because this is exactly the shunt-diode-pair case `chowdsp_wdf` was
vendored for and already provides a verified solution to; reinventing
Newton-Raphson for two antiparallel diodes when a tested implementation
already exists in the vendored dependency would be working against the
project's own "reuse what already exists" rule, not honoring it.

### 5. Tone / Level stack — passive, Big Muff-style

Confirmed by both the schematic and ElectroSmash ("passive Big Muff Pi
style tone control, which scoops the mids") to be the classic two-branch
blend: a bass/lowpass branch and a treble/highpass branch, mixed at a pot
wiper. Implemented as:

- Bass branch: R16 (6.8K) in series from Node CLIP (post C10, 0.01uF)
  to TONE-pot lug A, C12 (0.1uF) lug A to ground.
- Treble branch: C11 (0.022uF) in series from the same input node to
  TONE-pot lug B, R17 (6.8K) lug B to ground.
- TONE pot (VR2, 100KB) wiper blends the two; R15 (2.2K) in series from
  the wiper to the LEVEL pot's top lug (standard Big Muff practice —
  loads the tone wiper and sets the stage's overall insertion loss).
- LEVEL pot (VR3, 20KB): top lug from R15, bottom lug to ground, wiper
  is the final output tap. A simple bleed-to-ground volume control, not a
  gain stage (matches ElectroSmash's "-12dB overall loss" note on the
  equivalent stage in their revision).

**Fidelity note on this stage specifically**: the scanned schematic's
harness-numbered pot-lug connections (circles "8"/"6"/"7"/"5") were legible
enough to confirm the *topology class* (two RC branches into a blend pot,
loaded by a second volume pot) but not pixel-certain on which exact lug
each of R16/C12 vs C11/R17 lands on. This is the one place in this
processor where the implementation follows the standard, well-documented
Big Muff-family topology plus this schematic's own component-value
groupings (R16 was drawn adjacent to the "8"/lug-A column, C11+R15 shared
a column, C12+R17 shared a column) rather than a fully pixel-verified
trace — a deliberately lower-risk place to accept that uncertainty, since
it's a purely linear, secondary tone-shaping stage: getting a lug swapped
here shifts the tone sweep's character, it doesn't change whether the
circuit is stable, correctly biased, or captures the actual distortion
mechanism (which is fully pixel-verified above). Solved via ordinary
Thevenin/conductance-weighted-average reduction, no Newton-Raphson needed
(fully linear, resistors + `TrapezoidalCapacitor` companion models).

### 6. Output buffer — Q7 (JFET) treated as a closed switch, Q3 (BJT) emitter follower

Q7 (2SK30ATM **Y**-grade) sits right after the LEVEL pot's wiper. Q8, the
*other* Y-grade JFET on this board, is unambiguously part of the true-
bypass switching network (down with the LED driver, the flip-flop
transistors, and pin 14/D10). Two same-graded JFETs, one confirmed to be
a bypass switch, positioned identically relative to their respective
signal path (right where a "pass the wet signal when engaged" switch
would sit) — read as Q7 also being a bypass-switching element (biased
hard-on/hard-off by the footswitch logic, not a continuous signal-shaping
VCR like Q6, which sits between two coupling caps with no relation to the
switching section at all). Since this project's own bypass is handled at
the `EffectProcessor` level (see "Not modelled" above), Q7 is modelled as
a fixed, negligible series resistance (its on-state) rather than a second
JFET solve — flagged explicitly as an interpretation, not a pixel-certain
read, but a well-supported one.

Q3 (2SC732TM GR) is the real output buffer: a plain emitter follower, C13
(0.047uF) coupling in, R21 (10K) emitter resistor, C14 (1uF)/R23 (100K)
coupling out — mirrors Q1's input buffer exactly, and ElectroSmash's own
analysis of their revision's equivalent stage says exactly that ("mirrors
the input stage... unity gain and low output impedance"). Reused:
`EbersMollBJT`, independent per-terminal Thevenin (no feedback bridge
here, unlike Q2).

## Component values (this schematic's own designators)

| Ref | Value | Role |
|---|---|---|
| R1 | 1K | input series (folded into source impedance) |
| C1 | 0.047uF | input DC block |
| R2 | 470K | Q1 base bias, to BIAS1 |
| R3 | 10K | Q1 emitter resistor |
| C2 | 0.47uF/50 | Q1 emitter -> Q6 channel coupling |
| R4 | 100K | Q6 channel node A bias, to BIAS1 |
| R5 | 1M | Q6 channel node B bias, to BIAS1 |
| C3 | 0.047uF | Q6 channel -> Q2 base coupling |
| R6 | 100K | Q2 base bias, to true ground |
| R7 | 470K | Q2 collector-to-base shunt feedback |
| C4 | 250pF | Q2 feedback treble-cut |
| R8 | 10K | Q2 collector load, to V+ |
| R9 | 22 ohm | Q2 emitter resistor (most of this stage's gain) |
| C5 | 0.47uF/50 | Q2 collector -> op-amp pin 3 coupling |
| R11 | 100K | op-amp (+) input bias, to BIAS1 |
| R13 | 4.7K | op-amp feedback "Rg", (-) input to true ground |
| C8 | 1uF/50 | with R13 |
| R14 | 2.2K | op-amp output -> Node MID (feedback tap + forward path) |
| VR1 (DIST) | 100KB linear | op-amp feedback "Rf" |
| C9 | 0.47uF NP | Node MID -> Node CLIP coupling |
| D4, D5 | small-signal Si (1N4148-class) | anti-parallel hard clipper, to BIAS1 |
| C10 | 0.01uF | Node CLIP -> tone stack coupling |
| R16 | 6.8K | tone bass branch series |
| C12 | 0.1uF | tone bass branch shunt |
| C11 | 0.022uF | tone treble branch series |
| R17 | 6.8K | tone treble branch shunt |
| VR2 (TONE) | 100KB linear | tone blend |
| R15 | 2.2K | tone wiper -> level pot series |
| VR3 (LEVEL) | 20KB linear | output bleed/volume |
| C13 | 0.047uF | Q3 base coupling |
| R21 | 10K | Q3 emitter resistor |
| C14 | 1uF/50 | Q3 output coupling |
| R23 | 100K | Q3 output bias |
| supply | 9V battery | -> BIAS1 = supply/2 |

Transistor parameters (2SC2240GR/2SC3378GR for Q1/Q2, 2SC732TM GR for Q3):
representative small-signal NPN silicon values, same `Is`/`Vt`/`betaF`/
`betaR` starting point as the booster's PNP (mirrored) — see that doc for
why these are reasonable, adjustable assumptions rather than pinned specs.
JFET (2SK30ATM GR, Q6): representative small-signal N-channel JFET
parameters (`Idss` ~2-6mA, `Vp` ~-2 to -3V typical for this part's GR
grade, `lambda` ~0.02/V) — same "documented, adjustable assumption" status.
Pot tapers: "B" suffix is Japanese/ALPS convention for **linear** taper —
all three pots (VR1/VR2/VR3) are linear, unlike the booster's log-taper
approximation for its single audio-taper pot.

## Reused building blocks

- `EbersMollBJT` — Q1, Q2, Q3 (3 independent instances; Q2 additionally
  needs the one-sample-delayed R7/C4 cross-feedback term, see above).
- `ShichmanHodgesJFET` (new this pedal) — Q6.
- `TrapezoidalCapacitor` — every coupling/bypass cap (C1-C5, C8-C14).
- `chowdsp_wdf`'s `DiodePairT` + `ResistiveVoltageSourceT` (new use of
  the vendored library, first processor to actually instantiate a WDF
  element rather than direct nodal analysis) — D4/D5.

See [`CircuitFamilies.md`](./CircuitFamilies.md) for how this relates to
the booster and to future diode-clipper/tube-preamp circuits.
