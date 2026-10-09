# Rockerverb-Style Amplifier

A component-level model of the early **Orange Rockerverb 50 MK1** (4x6V6), built from the factory schematics
ORA-CD204 (preamp, 27 Feb 2004) and ORA-CD206 (power, 10 Mar 2004). **Both channels are modelled as drawn** (rebuilt
2026-10-08; the first version modelled only the Dirty channel, with several estimated values, a single Gain pot in
the wrong place and a generic tone stack -- see "2026-10-08 rebuild" below).

Display name: **Rockerverb-Style Amplifier** (trademark-safe convention).

## 2026-10-08 rebuild (read first)
Transcribed stage by stage from ORA-CD204 (300 dpi render of the scan, values legible):
* **Input**: C24 220n -> R42 1M0 shared grid leak -> R47 68K (V9-A) and R44 68K (V10-A).
* **Dirty**: V9-A (R37 100K, R46 1K5 + C27 10u; C22 not fitted) -> C31 1n0 -> R53 220K -> R60 220K + C42 470p ->
  **Gain RV4-B** (A1M, bright C36 100p) -> V9-B (R38 100K + C18 100p, R48 1K0 + C28 10u) -> C23 2n2 -> R54 220K ->
  R61 470K -> **Gain RV4-A** (second gang of the same pot) -> V8-A (R39 100K + C19 100p, R49 2K2 + C29 10u) -> C32 4n7
  -> R51 470K / R52 220K -> V8-B (R40 100K, R50 1K5 unbypassed) -> tone stack: C37 560p -> **Treble RV7 250KB**,
  R62 39K slope, C40 22n -> **Bass RV5 500KA** (rheostat) -> **Middle RV6 25KB**, C41 22n -> RV6 wiper ->
  **Volume RV8 500KA**.
* **Clean**: V10-A (R35 100K, R43 1K5 + C25 22u) -> C30 1n0 -> R56 220K / R55 220K -> **Volume RV1 500KA** (bright
  C34 150p) -> V10-B (R36 100K, R45 1K5 + C26 22u) -> tone stack: C35 56p -> **Treble RV3 250KB**, R58 100K slope,
  C38 22n -> **Bass RV2 250KA**, C39 22n -> RV2 wiper with **R57 6K8** to ground (the fixed "middle": no Middle knob).
* **Relay RL1** picks one channel; then shared: R4 220K -> C4 220n -> **V7-A (12AT7) cathode follower** (R15 22K,
  grid biased from rail D by R17 220K / R14 33K + C11 via R16 1M) -> C5 220n -> R6 150K / R13 68K -> loop -> C1 220n,
  R20 1M, R7 68K -> **V7-B (12AT7)** (R18 56K, R19 1K5 unbypassed) -> C6 220n -> R9 1M -> reverb mixer (RV9 250KA at
  minimum + R64 220K, R63 1M) -> C3 47n -> PI grid (R26 1M).
* **No Presence** on this amp (the NFB is a fixed R10 4K7 into the PI tail) -- the old Presence knob was removed.
  **No master volume**: Power Drive (page 2) now defaults to 1.0 = the real amp.
* Rails: F (V9/V10) = sE, E (V8) = sD, D (V7) = sC, C (PI) = sB.

Measured (AmpDynamicsBench, 196 Hz sine): Clean at noon 2-3% THD with a normal pickup level, breaking up as Volume
rises (12% at full, 20% with a hot input); Dirty from a light crunch at Gain 2 (4%) through crunch at 4 (20%) to a
thick 37% at full. Registry trim -14.3 -> -5.74 dB.

## Architecture

**Dirty channel only** is modelled. Clean channel switching, effects loop, built-in reverb, and
footswitch logic are not implemented. Solid-state rectification (4×1N5408).

Two-block architecture: pre, power — split at V8-B's plate where the AC-coupled output feeds the
tone stack. Same pattern as the EVH 5150, SLO-100, Mark IIC+, and Dual Rectifier.

### Preamp (four cascaded 12AX7 gain stages: V9-A → V9-B → Gain → V8-A → V8-B)

1. **V9-A** (R32=100K plate, R47=68K grid stopper, 1M grid leak, R46=1K5 cathode, C27=10µF bypass)
   - Input stage. Coupling via C24=220nF from input.

2. **C31=1nF coupling** → **V9-B** (R38=100K plate, R53=220K grid leak, 1K5 cathode, 10µF bypass)
   - Second gain stage. 10K estimated series stopper.

3. **RV4=1MA Gain pot** (audio taper, split rGainTop/rGainBot)
   - Coupled through estimated 22nF + 470K bias reference.

4. **V8-A** (100K plate, 220K grid leak, 1K5 cathode, 10µF bypass)
   - Third gain stage. 10K series stopper.

5. **V8-B** (100K plate, 220K grid leak, 1K5 cathode, 10µF bypass)
   - Fourth gain stage. 22nF coupling from V8-A.

The preamp block runs on two rails: sE (~320V, V9 stages) and sD (~330V, V8 stages). V8-B's plate
is AC-coupled to the power block; the DC component `plateDcV8b` is subtracted before feeding the
tone stack input source.

### Power block (tone stack + Master + PI + 4×6V6 + OT + speaker + NFB)

**Tone stack** (FMV-derived): 38K source impedance (100K∥62.5K), 560pF treble cap, 39K slope,
22nF bass cap, 22nF mid cap, 250KB treble pot, 1MA bass pot, 25KB mid pot.

**Master**: 100nF coupling from tone output → 500KA rheostat (audio taper) to ground → 1M load.

**Phase inverter**: V5-A/V5-B 12AX7 long-tailed pair. 82K/100K plate loads, 10K+47K tail,
100nF coupling from master to grid A, 47nF cross-coupling from PI plate B to grid B, 1M grid leaks.
PI triode uses kg1×40 for stability (same pattern as all other amps in this project).

**Power tubes**: 4×6V6 modelled as two push-pull pairs (merged parameters: kg1×0.5, kg2×0.5,
Gg×2.0). Published Koren 6V6GT: kg1=1400, then ×0.5 pair merge ×40 stability = 28,000 (same
magnitude as the EVH 5150's 6L6GC pair at 29,200). Fixed bias at -40V. 1K0 grid stoppers, 220K
grid leaks to bias node. Cathodes grounded (same proven pattern as 5150/Powerball — the schematic
shows individual 1K5 cathode resistors but the model uses the fixed-bias ground pattern for
consistency with the rest of the project's power stages).

**Output transformer**: 5.0H primary half-inductance, 11.18:1 half-to-secondary turns ratio
(scaled from Deluxe Reverb — similar 6V6 tube type), k_halves=0.9997, k_secondary=0.997.
55Ω primary-half resistance, 0.15Ω secondary.

**NFB**: 4K7+150K combined (154.7K) from OT secondary to PI grid B. Presence pot (250K) shunts
the feedback node to ground. 10nF HF feedback cap + 1.5nF stray for stability.

**Speaker**: standard Re + Le + parallel-RLC cone resonance. 4/8/16 Ω selectable, matched at 8Ω.

Power block uses theta=0.9 (slightly damped trapezoidal).

## Supply model

410V nominal, 60Ω rectifier resistance, 100K bleeder.

Filter chain: 47µF → 4K7 → 47µF (screens/PI) → 4K7 → 22µF (V8 preamp) → 4K7 → 22µF (V9 rail D)
→ 10K → 22µF (V9 rail E).

Current sources iA (plate), iB (screen+PI), iC (V8 preamp), iD (V9 D rail), iE (V9 E rail) are
measured from the full model's DC loop and updated every 8 samples. Only power block sources
(wSrcCt, wSrcPi) and vScreen are updated per-sample from the supply model.

## Reduced-order mode

Behavioral power stage (fitted curve + sag table + shelf), same pattern as all other amps.
The preamp block always runs its full NodalCircuit netlist. Reduced-order is the default
(shipped via `reducedOrder = true` in registry).

Sag table scaled from Deluxe Reverb measurements (same 6V6 tube type, 415V→410V).

## 6V6 pentode parameter note

The Deluxe Reverb's `pentode6V6()` uses kg1=9000. That is already a stability-raised value from
the published Koren 6V6GT kg1=1400. The Rockerverb uses the published value (1400) as the base,
then applies pair merge (×0.5) and stability (×40), giving kg1=28,000. Using the Deluxe Reverb's
already-stabilized 9000 as a base would have produced kg1=180,000, making the power tubes
completely non-conductive at the -40V bias.

## Bias and operating point

| Node | Voltage |
|------|---------|
| Plate rail (sA) | ~410V |
| Screen rail (sB) | ~359V |
| V8 preamp rail (sC) | ~339V |
| V9 rail D (sD) | ~330V |
| V9 rail E (sE) | ~319V |
| V9-A plate | ~216V |
| V9-B plate | ~229V |
| V8-A plate | ~229V |
| V8-B plate | ~229V |

## Simplifications

- Dirty channel only; Clean not modelled.
- No effects loop, reverb, or footswitch logic.
- Grid stopper resistors estimated (~10K) where not legible on schematic.
- Coupling cap values between V9-B/Gain and V8-A/V8-B estimated (22nF).
- Power tube cathodes grounded instead of individual 1K5 resistors.
- 470K bias reference and gain pot coupling values estimated from similar circuits.
- Supply sag table scaled from Deluxe Reverb measurements (same 6V6 tube type).

## Test results

| Test | Result |
|------|--------|
| DC convergence | OK (all rails in range) |
| Silence-settle peak (10s) | 0.004 (< 0.025) |
| Solver failure rate | 0.000000 (< 1e-6) |
| Pluck peak (5s) | 0.002 (< 5.0) |
| Pluck failures | 0.000000 |
| reducedOrder pluck peak | 1.214 |
| reducedOrder pluck rms | 0.760 |
| reducedOrder CPU | 10.4% |
| Unity trim | -14.3 dB |
