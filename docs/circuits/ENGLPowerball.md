# ENGL Powerball-Style Amplifier

A component-level model of the **ENGL Powerball (645-1)** Hi Lead channel, built from the factory
ENGL Powerball schematic. The Powerball is one of the flagship high-gain amplifiers from ENGL,
known for its tight, aggressive distortion and versatile four-channel design. Only the Hi Lead
channel is modelled here.

Display name: **Powerball-Style Amplifier** (trademark-safe convention).

## Architecture

**Hi Lead channel only** is modelled. Channel switching, effects loop, built-in reverb/noise
gate, and MIDI control are not implemented. Solid-state rectification (BY509 bridge).

Three-block architecture: pre, tone, power — split at the coupling cap between U6A's plate and
the tone stack, and between the Master wiper and the phase inverter. The tone block is the critical
design challenge: 3 cascaded post-tonestack triodes (U6B → U7A → U7B) produce ~230,000× open-loop
DC voltage gain, which amplifies trapezoidal companion model DC leakage into runaway drift.

### Preamp (three cascaded 12AX7 gain stages: U5A → U5B → U6A)

1. **U5A** (R3=100K plate, R1=1M grid leak, R2=1K cathode, CE1=22µF bypass)
   - Input stage with ~10K grid stopper.

2. **47nF coupling** → **470K bias ref** → **P10=1MA Gain pot** (audio taper, split rGainTop/rGainBot)
   - CH2 pre-gain control.

3. **U5B** (R9=100K plate, R7=1M grid leak, R8=1.5K cathode, CE2=22µF bypass)
   - Second gain stage with ~10K series stopper.

4. **U6A** (R20=330K plate, R14=470K grid leak, R19=3.3K cathode, CE4=1µF bypass)
   - Third gain stage. High-impedance plate (330K vs typical 100K) provides extra voltage
     gain and earlier clipping — a distinctive Powerball voicing choice.

The preamp block runs on `sD` rail (~418V). U6A's plate is AC-coupled to the tone block;
the DC component `plateDcU6a` is subtracted before feeding the tone block input source.

### Tone block (FMV tone stack + U6B + U7A + U7B + Master)

**Tone stack** (FMV-derived): R32=330K slope, P14=250KB treble, P11=1MA bass,
P15=250KB focused mid. 470pF treble cap. Input impedance modelled as
R20(330K) ‖ ra(62.5K) ≈ 52K source resistance.

**Post-tonestack gain recovery** (three cascaded stages):

1. **U6B** (R24=100K plate, R22=1M grid leak, R23=1.5K cathode, CE5=22µF bypass, 100nF coupling from tone stack)
2. **U7A** (R43=100K plate, 1M grid leak, R42=1K cathode, CE6=4.7µF bypass, 10nF coupling from U6B)
3. **U7B** (R49=100K plate, 1M grid leak, R48=1.5K cathode, CE7=22µF bypass, 100nF coupling from U7A)

**Master**: 100nF coupling from U7B plate → 1M rheostat (rMaster, audio taper) to ground.

The tone block's VCC (`sC` rail, ~426V) is set once in `prepare()` and NOT updated by the
per-sample supply model. This is critical: the three cascaded triodes have ~62× gain each,
giving ~230,000× open-loop DC gain. Any supply ripple fed into this block would be amplified
into massive DC drift via two feedback paths:
1. **Direct**: supply → tone VCC → 230,000× amplification
2. **Indirect**: supply → preamp VCC → plate voltage → acU6a → tone → 230,000× → power → supply

#### DC servo

Even with static VCC, the trapezoidal companion model's conductance (2C/T ≈ 0.0096 S for 100nF
at 48kHz) overwhelms the 1M grid leaks (1e-6 S) by ~10,000:1, making the coupling caps
effectively transparent to DC at the solver's resolution. The Newton solver's ~300µV tolerance
per node × 230,000× cascaded gain = ~69V of drift.

A **DC servo** (slow integrator) feeds back the master wiper's DC error to the tone block's
input source, counteracting the drift at its origin:
```
mwServo += 1e-3 * (masterWiper - mwTarget)
toneInput = masterGain * acU6a - mwServo
```
The loop gain is 1e-3 × 230,000 = 230, fast enough to dominate the drift. Combined with a
0.5Hz HPF DC blocker on the master wiper and U7B output, this keeps the silence-settle peak
below 0.025 (measured 0.022) with zero solver failures.

### Power section (PI + power amp + OT + speaker + NFB)

**Phase inverter**: U8A/U8B 12AX7 long-tailed pair. R61=82K/R62=100K plate loads,
R53=100K cross-coupling from plate B to grid A. 47nF coupling from PI plates to power tube
grids. 1.5K cathode + 4.7K tail.

**Power tubes**: 4 × 6L6GC modelled as two push-pull pairs (merged parameters: kg1×0.5,
kg2×0.5, Gg×2.0). Fixed bias at -55V. 470Ω screen resistors (R58A/B). 330K grid leak to
bias node. 2.2K grid stoppers.

**Output transformer**: 3H primary half-inductance, 6.25:1 half-to-secondary turns ratio,
k_halves=0.9997, k_secondary=0.995. 45Ω primary-half resistance, 0.15Ω secondary.

**NFB**: R56=22K from OT secondary to PI grid B (scaled to 44K for 8Ω matched tap).
Presence pot (250K) in series with feedback path. Depth pot (250K) bypasses the NFB
with a shelf filter.

**Speaker**: standard Re + Le + parallel-RLC cone resonance. 4/8/16 Ω selectable.

Power block uses theta=0.9 (slightly damped trapezoidal). PI triodes use kg1×40 for stability
(same pattern as all other amps in this project).

## Supply model

460V nominal, 50Ω rectifier resistance, 220K bleeder.

Filter chain: CE8=47µF/500V → R136=470Ω → CE9=47µF (screens) → R137=2.2K → sC (PI/post-
preamp, 22µF) → R138=2.2K → sD (preamp, 10µF).

Current sources iA (plate), iB (screen), iC (PI+post-preamp), iD (preamp) are measured from
the full model's DC loop and updated every 8 samples. **Only power block sources (wSrcCt,
wSrcPi) and vScreen are updated per-sample from the supply model** — preamp and tone VCC are
frozen at their prepare() values to prevent the 230,000× supply-to-output feedback loop.

## Reduced-order mode

Behavioral power stage (fitted curve + sag table + shelf), same pattern as all other amps.
The preamp and tone blocks always run their full NodalCircuit netlists. Reduced-order is the
default (shipped via `reducedOrder = true` in registry).

## Bias and operating point

| Node | Voltage |
|------|---------|
| Plate rail (sA) | ~460V |
| Screen rail (sB) | ~450V |
| PI/post rail (sC) | ~426V |
| Preamp rail (sD) | ~418V |
| U5A plate | ~253V |
| U5B plate | ~280V |
| U6A plate | ~225V |
| U6B plate | ~285V |
| U7A plate | ~257V |
| U7B plate | ~286V |
| PI tail | ~12V |
| Power grid bias | ~-51V |

## Simplifications

- Hi Lead channel only; Clean/Crunch/Lead not modelled.
- No effects loop, reverb, noise gate, or MIDI.
- Channel-switching relay circuitry omitted.
- Grid stopper resistors estimated (~10K) where not legible on schematic.
- Preamp and tone VCC frozen (not tracking supply sag) to prevent DC runaway.
- DC servo at tone input (not present in the real circuit) to counteract numerical drift.
- Supply sag table scaled from Twin Reverb measurements (same approach as other amps).

## Test results

| Test | Result |
|------|--------|
| DC convergence | OK (all rails in range) |
| Silence-settle peak (10s) | 0.022 (< 0.025) |
| Solver failure rate | 0.000000 (< 1e-6) |
| Pluck peak (5s) | 0.047 (< 5.0) |
| Pluck failures | 0.000017 |
| reducedOrder pluck | 1.4 peak, 1.3 rms |
| reducedOrder CPU | ~15% |
| Unity trim | -22.5 dB |
