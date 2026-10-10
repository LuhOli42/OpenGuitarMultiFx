# Dumble Steel String Singer — `DumbleSteelStringStyleAmplifier`

## Sources

No public factory schematic exists for the Steel String Singer; the design was reverse-engineered from
gut shots of serial #002 (1981, the Jackson Browne amp) and the widely circulated "hand-drawn schematic"
derived from them. What is well established across #001–#005: 4x6L6GC ~100 W, Fender-by-Schumacher
transformers, 12AX7 phase inverter, and — the SSS signature — a DC-coupled cathode-follower (DCCF)
driver stage between the PI and the power grids (#004 onward dropped the DCCF for a standard LTPI).

## What the amp actually is

Howard Dumble's ultra-clean flagship. Everything about it is built for headroom: high B+ (~460–500 V on
the plates), a big output transformer, a FET input stage that isolates the guitar, and output-tube drivers
that can swing the grids without sagging. Stevie Ray Vaughan's amps (#006–#009) are this model.

## What is modelled

- Preamp: V1A input stage (220K plate, 1.8K + 22µF cathode) → .047µF → **Volume** (1MA) → V1B recovery
  (100K plate, 1.5K unbypassed) → .022µF → V2A (220K, 1.8K + 4.7µF) → V2B cathode follower.
- Dumble-style TMB stack (250pF treble cap, 100K slope, .022 bass/mid caps, 250K treble / 1M bass /
  25K mid, plus a 33K series leg) then a 1M Master rheostat, kept in the `power` block like every sibling.
- 12AX7 long-tailed-pair PI (82K/100K plates, 1K + 10K tail, .022µF input cap, 47pF plate compensation)
  with global NFB from the 16Ω tap through an estimated 100K feedback resistor and a 25K presence shunt.
- 4x6L6GC as two push-pull pairs (.047µF couplings, 220K grid leaks, 1.5K stoppers, fixed bias −51V),
  3H primary-half OT, resonant speaker model.
- Supply: rectifier → 220µF + 150K bleeder → 10H choke → 220µF → 2.7K → PI rail → 22K → V2 rail → 15K →
  V1 rail, all sagging dynamically off measured pentode/preamp currents.
- `reducedOrder` behavioural power stage (same fitted constants as the 6L6GC siblings) when the quality
  tier demands it.

## Deliberate simplifications

- **DCCF driver stage modelled as conventional capacitive coupling.** The real #002 couples the PI plates
  to the power grids through DC-coupled cathode followers; the codebase makes the same simplification for
  the SLO-100 (whose topology is the same). The audible consequence — slightly less immediacy of attack —
  is acknowledged, not hidden.
- FET input stage omitted (it's a buffer; `inputLimit` covers its role).
- Hi/Lo filter network around V1B omitted — it's the amp's "tone-sculpt" section; the TMB covers the EQ.
- Rock/Jazz switch, boost, reverb send/return, PAB: not modelled (single clean voice).
- The NFB resistor value (one of the per-unit Dumble tweaks) is estimated at 100K.

## Controls

Page 1 mirrors the front panel: Volume, Treble, Middle, Bass, Presence, Master.
Page 2 (synthetic): Power Drive, Bias, Tube Feel, Speaker (4/8/16Ω), Output.

## Stability / verification

- kg1×40 on PI triodes and power pentodes (the codebase-wide LTP stabilization).
- DC: plates ~470V, screens ~464V. Unity trim is calibrated so the PedalUnityLevel suite passes.
- `Tests/DumbleSteelStringStyleAmplifierProcessorTests.cpp`: DC convergence, silence settle, pluck.
