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

- Preamp, rebuilt against Ryan Colgan's LTSpice netlist of #002 ("SSS No 002.net"), in the real
  stage order: V1A (100K plate, 68K||68K stopper, 1.5K + 5µF cathode onto the 100-Ω LNFB tail) →
  the #002 distributed tone stack → **Volume** (1MA, with the real 250pF **Bright** cap across
  the pot) → V1B (100K plate, 1.5K + 5µF on its own 100-Ω tail) → .01µF + 300mH choke → **Hi/Lo
  7-position filter bank** (the tapped 39K/68K/100K/180K/270K/390K string into a 12K tail, and
  the 150p–.01µ cap bank against the 470K/1nF/3nF/820K filter node) → V2A (100K plate, 1K + 1µF)
  → V2B cathode follower → C49 .01µF → **Master** → PI.
- **LNFB loop**: follower output → .22µF + 100K leak → 470K back into V1A's 100-Ω tail
  (C47/R83/R82), AC-only feedback as on the real amp.
- #002 distributed tone stack (NOT a Fender FMV): treble 360p → 250K-A pot whose wiper is the
  output node (bottom on the 4.7nF C35 AC ground); bass/mid feed R3 100K → C3 .1µ → 250K-A bass
  pot (bottom on 1.8K tail, wiper merged through R63 100K) and C34 .047µ → 100K mid rheostat;
  then a 1M-A Master.
- Front-panel switches, wired where the real circuit puts them: **Bright** (250pF across Volume),
  **Deep** (swaps the direct link for the R64/R65 270K shelf on the R66 1M || C36+R67 network),
  **Accent** (1nF across the Master's top leg), **Jazz/PAB/Rock** (Jazz = the real MIC pad —
  ties the treble-bottom network onto the bass wiper and tightens the coupling cap; PAB = the
  boost position, lifts the bass/mid legs AND the bass-wiper merge so only the treble path
  remains; Rock = neutral), **Mid Boost** (4.7nF treble-side feed into the mid leg — an
  ODS-family switch the #002 doesn't have; documented interpretation).
- #002 master placement: follower → .01µF (C49) → Master 1M-A → PI, with ACCENT's 1nF across
  the pot's top leg (C50). Master sits in the `power` block so `wTone` remains the PI input.
- 12AX7 long-tailed-pair PI with #002 values (100K/220K plates, 820Ω + 18K tail, .02µF input,
  68pF plate compensation) and global NFB from the 16Ω tap through an estimated 100K feedback
  resistor and a 25K presence shunt.
- 4x6L6GC as two push-pull pairs (.047µF couplings, 220K grid leaks, 1.5K stoppers, fixed bias
  −51V), 3H primary-half OT, resonant speaker model.
- Supply: rectifier → 220µF + 150K bleeder → 10H choke → 220µF → 2.7K → PI rail → 22K → V2 rail →
  15K → V1 rail, all sagging dynamically off measured pentode/preamp currents.
- `reducedOrder` behavioural power stage (same fitted constants as the 6L6GC siblings) when the
  quality tier demands it — it consumes the same `wTone` node, so the new stack/switches affect
  both paths identically.

## Deliberate simplifications

- **DCCF driver stage modelled as conventional capacitive coupling.** The real #002 couples the
  PI plates to the power grids through DC-coupled cathode followers; the codebase makes the same
  simplification for the SLO-100 (whose topology is the same).
- FET input stage omitted (it's a buffer; `inputLimit` covers its role).
- The clone's GUITAR/MIC dpdt is mapped onto the **Voicing** switch: Guitar ↔ Rock, Mic ↔ Jazz
  (Mic ties the treble-bottom network onto the bass wiper AND tightens the V1B→choke coupling
  to 4.7nF), with the middle position implementing the #003+ PAB leg-lift — a documented
  interpretation, since the two real switches share the same circuit region. Deep is wired the
  way #002 has it: the bass wiper feeds the R64/R65 shelf straight into the Volume wiper node,
  bypassing the pot. Reverb send/return not modelled.
- The NFB resistor value (one of the per-unit Dumble tweaks) is estimated at 100K; the real amp's
  small plate→PI local feedback (R20 2.7K) is not added — the speaker-tap loop covers the role.

## Controls

Page 1 is the front panel: Volume, Treble, Middle, Bass, Presence, Master, and its switches -- Filter (Hi), Filter (Lo)
(the real 7-position rotaries), Bright, Mid Boost, Deep, Accent, Voicing (Jazz / PAB / Rock).
Page 2 (synthetic): Power Drive, Bias, Tube Feel, Speaker (4/8/16Ω), Output.

## Stability / verification

- kg1×40 on PI triodes and power pentodes (the codebase-wide LTP stabilization) -- REMOVED; the
  instability it suppressed no longer reproduces (see CircuitFamilies.md's dead-reference repair
  section).
- DC: plates ~470V, screens ~464V. Unity trim is calibrated so the PedalUnityLevel suite passes.
- `Tests/DumbleSteelStringStyleAmplifierProcessorTests.cpp`: DC convergence, silence settle, pluck.
