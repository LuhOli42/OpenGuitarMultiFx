# NodalCircuit — the netlist solver

`Source/Effects/NodalCircuit.h`. A small SPICE-style solver (modified nodal
analysis, trapezoidal capacitors, Newton-Raphson for nonlinear devices),
sized for one stompbox stage at audio rate. It exists because the pedals it
runs are **not chains**: the Klon Centaur's two feed-forward networks (one
referenced through the second gang of the Gain pot) and summing amp all
interact; the BD-2's gain stages are discrete JFET/PNP op-amps with the Gain
pot in their feedback; the HM-2's tone section is three gyrators hung on two
pots. The earlier pedals here were solved by hand-deriving a Thevenin
reduction per stage, which stops being trustworthy once stages interact.
The TS808/9/10 and OD-1 processors are still hand-derived; the DS-1 was ported
to a netlist (its hand-derived version coupled stages one sample late, which
made it chaotic at high gain -- non-periodic error -5 dB, now -322 dB).

## Elements
Resistors, capacitors (trapezoidal companion, same as `TrapezoidalCapacitor`),
driven nodes (rails, the input), **ideal op-amps** (output row replaced by the
virtual-short constraint, so no extra unknown), Shockley diodes, BJTs and
N-channel JFETs. The BJT and JFET equations are `EbersMollBJT::evaluate()` and
`ShichmanHodgesJFET::evaluate()` — one home for each device model, shared
with the older processors.

## How a sample is solved (the "DK method")
The linear part is LU-factored once and again only when a resistance changes
(pots are updated at control rate, every 16 samples, smoothed). Each nonlinear
device exposes a few *ports* (a diode: 1 branch voltage; a BJT or JFET: 2).
Per sample the linear network is back-substituted once, then Newton-Raphson
runs **only over the port voltages** (a handful of unknowns), not the whole
matrix. The solution is identical to a full Newton solve.

Robustness work that turned out to matter (each was a real failure found by a
test, not a guess):

- **DC operating point by pseudo-transient relaxation** (backward-Euler steps
  of growing size from the initial guess), falling back to a static
  source-stepped Newton. A raw Newton on the static equations diverged on the
  BD-2's gain stage (high loop gain). It also means a circuit starts *settled*
  instead of needing seconds of silence like the older processors.
- **SPICE `pnjlim` junction limiting** on diode and BJT ports. Without it, a
  diode network behind a large resistance (the HM-2's clipping op-amp) needed a
  ~740 V no-device response to be pulled back to ~0.6 V: plain Newton overshoots
  the exponential and then sheds only ~Vt per iteration, failing 4-18% of
  samples. With it: 0.
- **Convergence is MEASURED, not estimated.** After each Newton step the devices are
  evaluated at the new point; the step solved the linearised equations exactly, so the
  whole leftover error is `cur_actual - (cur_before + D * step)`, and pushing that through
  `W` gives the error in every node voltage -- the thing the audio hears. The point is
  accepted when that is below 10 uV, and the currents evaluated AT it are used for the
  reconstruction (no stale currents). The previous rule (accept when the step's estimated
  node change was < 300 uV, linearised at the point the step *started* from) turned out
  to leave errors of 0.1-0.5 mV against a 1 nV reference (a diode turning on has slope ~0
  at the start, so a huge step "changes nothing"); this one agrees with the reference to
  a few uV, and the evaluation it needs is the one the next iteration would have done
  anyway (Newton iterations are 1.0-1.3 per sample in practice).
- **Newton warm start**: the previous port voltages extrapolated linearly by
  the last step, with junction ports passed through the same `pnjlim` limit.
  (Unlimited, the extrapolation threw a fast edge into exp() overflow and made
  the BD-2 fail 98% of its solves.) Cuts the average iterations per sample by
  ~10%.
- **`EbersMollBJT::solve()` uses `pnjlim` too** (damped so the vbe/vbc steps
  obey it) with 40 iterations instead of 12. Without it the Booster's and DS-1's
  transistors failed to converge on ~1-2% of samples; a failed solve holds the
  previous value, which is a click -- hiss at that rate. Now 0 failures at 1x.
- **Capacitors integrate with theta = 0.6, not the trapezoid's 0.5** (`i_eq =
  g v + (1-theta)/theta i`, `g = C fs / theta`). The trapezoid rule has no damping
  at Nyquist, so a stiff switching node (the HM-2's diodes, the DS-1's clipper)
  rings at fs/2 for a long time, which is the "harsh, artificial" top end.
  theta = 0.6 cuts that ringing energy above 12 kHz by ~9 dB and moves the audible
  band by 0.03 dB. `NodalCircuit::defaultTheta` / `setIntegrationTheta()`;
  the closed-form solver tests set 0.5 explicitly.
- **Never cut a block where the downstream gain is high** (see the HM-2 doc): a
  one-sample-delayed or predicted coupling is multiplied by whatever gain follows.
- Initial guesses matter for the relaxation: a guess that contradicts the DC
  point (4 V on a node the diodes hold at 0 V) hands the diodes a huge current.

## What was simplified to make it cheap (2026-09-20), and how each was checked
Same sound, fewer Newton dimensions. Every change below was compared against the
previous solver on 12 renders (6 pedals x 2 gain settings, a decaying three-note chord);
"timbre" = octave-band spectral shape with the level removed, worst band:

- **Antiparallel diodes are one device with one port** (`i = Is1(e^{v/n1}-1) - Is2(e^{-v/n2}-1)`):
  exact (-170 dB vs before).
- **A BJT whose collector is on a fixed rail is a one-port device** (vbe only; its
  collector junction is ~1e-15 A): exact.
- **A JFET marked `assumedVds` is a one-port device** (channel-length modulation taken at
  that vds; used for the BD-2's tail-pair devices with their drain on the rail): BD-2 timbre
  0.7 dB.
- **Emitter/source followers that only buffer are ideal followers** (`addFollower(in, out,
  drop)`: out = in - drop, no base current, no Newton port): DS-1 Q1/Q3, HM-2 Q1, BD-2
  Q3/Q7/Q1, TS Q1/Q2, OD-1 Q6/Q7. Timbre within 0.04-0.3 dB (BD-2 0.8), waveform ~-28 dB
  because a 1-5% gain change is amplified by the clipping stages after them (the pedals
  are re-trimmed to unity, so only the shape matters). Caveat learned in the OD-1: an
  ideal source straight across a capacitor makes the trapezoid rule ring at Nyquist
  forever, so the follower's output resistance (1/gm, 74 ohm) stays in that branch.
- **The DS-1's Q6 JFET is a 333 ohm resistor** (used as a VCR with vgs ~ 0 in the triode
  region; the signal there is < 0.3 V against Vp = -2 V): timbre 0.04 dB.

## Saturating op-amps (`addSaturatingOpAmp`)
A real op-amp on 9 V runs out of output swing (TA7136AP: "+1.5 V to Vcc - 1.5 V";
M5218AL and TL072 similar) long before the diodes after it clip, which changes what
the coupling capacitors after it see and how the stage recovers. An ideal op-amp is a
constraint row (V+ == V-) with unlimited output; clamping that output afterwards is
wrong, because the feedback network would still be driven as if the loop were closed.

Modelled as a **three-state switch, solved exactly**: ideal (V+ == V-), held at the
high rail, held at the low rail. In a held state the constraint row is replaced by
`V(out) = rail`, so the inverting input really stops following the non-inverting one
and the capacitors in the feedback network keep charging. Each state has its own LU
(built lazily, parked when not active, dropped when a resistance changes); a sample
tries the previous state first and switches when the answer is inconsistent (ideal:
output outside the rails; held: (+)-(-) no longer pushing into the rail), at most
four passes. Inside the swing it is bit-for-bit the ideal op-amp; outside, the
output sits exactly on the rail (`NodalCircuitTests`). No extra Newton port, and
because a saturated stage no longer drives tens of volts into the clipping diodes the
DS-1 got *cheaper* (44% -> 23% of a core at 4x). At most one per circuit; every
block that needs one has exactly one.

Tried first and rejected: a finite-gain VCVS behind an output resistance with clamp
diodes (unbounded currents, 0.5 V overshoot beyond the rail), and a nonlinear VCCS with
a tanh-like knee (Newton overshoots across the knee when it starts saturated: a
2-cycle between +-0.3 V). Both are stiff because the device's slope is ~0 exactly where
Newton needs it; the switch has no such region.

Where it is used: DS-1 (TA7136AP, rails 1.5 / 7.5 V -- the one it really changes: the
stage is followed by a shunt clipper so its output swings to the rails), HM-2 IC3A
(M5218AL), BD-2 IC1B (M5218AL), Klon gain stage (TL072, +-7.5 V about the 4.5 V
reference). The last three never reach their rails with a normal guitar signal, so
they sound the same; they are there so they do when driven hard.

## Verification (Tests/NodalCircuitTests.cpp)
RC low-pass, ideal op-amp with a series-cap gain leg, and a diode clipper match
closed-form/independent scalar solves to 5 digits; NPN, PNP and JFET DC points
match `EbersMollBJT::solve()` / `ShichmanHodgesJFET::solve()`; and the strongest
check: **a 21-unknown TS808 netlist reproduces the hand-derived
`TubeScreamerStyleOverdriveProcessor` to 0.013% / 0.002%** (two independent
methods, same circuit).

## Cost
| | unknowns | us/sample (1 channel) | % of one core @ 48 kHz |
|---|---|---|---|
| TS808 netlist (test) | 21 | ~0.45 | 2.2% |
| Centaur | 2 + 16 + 7 | ~0.5 | 2.4% |
| DS-1 | 2 blocks | ~0.7 | 3.5% |
| HM-2 | 3 blocks | ~1.6 | 7.8% |
| BD-2 | 5 blocks | ~1.7 | 8.1% |

(All at the host rate. The pedals with a clipper run through
[`OversampledEffect`](./Oversampling.md), which multiplies these by 2 or 4.)

Stereo doubles it unless the block is **dual-mono** (a mono guitar fed to both
channels): the processors then solve one channel and copy the output.
Identical input is not enough (the circuits' capacitor *states* must be
identical too), so the shortcut is used only while the channels are known to be
in sync; it switches off after any divergence and re-arms after 10 s of
identical input. Tested exact (0.000000000 error) across dual/different/dual.

## Limits (shared by every processor built on it)
- **Op-amps are ideal inside their swing**: no gain-bandwidth (parts in the MHz
  range: M5218AL 7 MHz, 4558 ~3 MHz, TL072 3 MHz -- closed-loop bandwidth GBW/gain stays
  above the audio band; the TA7136AP's GBW is not in its datasheet, assumed similar),
  slew rate (TA7136AP 0.5 V/us, M5218AL 3 V/us: only the edges of a hard square
  wave) or input protection. Output rail clipping IS modelled where it matters, see below.
  Discrete transistor stages saturate anyway (they are real transistors in
  the model).
- Gate current of a JFET is zero; BJT/diode series resistance and junction
  capacitances are not modelled.
- Blocks are cut where an ideal op-amp output / JFET gate / op-amp (+) pin
  cannot be loaded back. Where that isn't exact, the approximation is stated in
  the pedal's own doc (none currently: the HM-2's predicted node was removed by merging its op-amp into block 1).
- `maxUnknowns = 24`, at most 16 device ports per block.

Define `NODAL_DEBUG` to trace Newton iterations to stdout (never in the audio path).
