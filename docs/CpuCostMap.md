# Where the CPU goes (measured 2026-09-21)

Dev PC: AMD BC-250 (Zen 2, AVX2), 48 kHz, 128-sample blocks, stereo dual-mono guitar-like input, every effect at its
defaults, eco rendering quality. **% of ONE core**, "playing" / "digital-silence tail". Reproduce with
`EFFECT_BENCH=1 [BENCH_NAM=<a .nam>] [BENCH_IR=<a .wav>] [BENCH_FTZ=1] build/Tests/.../OpenGuitarMultiFx_Tests EffectCostBench`
(`Tests/EffectCostBench.cpp`; it does nothing unless `EFFECT_BENCH` is set).

| Effect | playing | tail | Note |
|---|---|---|---|
| Neural Amp + Cab, real WaveNet model (Marshall YJM100) | **6.8** (was 21.6) | 6.7 | `nam_core` built with `-march=native -ffast-math` (below) |
| Cab, real 4 s IR (Celestion V30) | **1.7** (was 8-18) | 1.6 | IR capped to 150 ms for the Cab role (below) |
| Reverb role with a 4 s IR | 7.5 | 17.5 | a reverb IS its tail; not capped |
| Bassman-Style Amplifier | 16.7 | 10.9 | 8 tubes + transformer + feedback + supply, one Newton solver |
| BD-2 | **2.6** (was 7.4) | 2.5 | macro-model gain stages (`BD2StyleOverdrive.md`) |
| HM-2 | 6.6 | 4.2 | a one-port-transistor variant (5.4%) exists but is not the default: see `HM2StyleDistortion.md` |
| Distortion+ / DOD 250 / RAT / Guv'nor / Blues Breaker | 1.7 / 2.1 / 2.7 / 4.3 / 5.0 | 1.5 / 1.6 / 1.9 / 3.4 / 3.9 | op-amp macro-models + diodes, default quality (`DistortionPlusStyleDistortion.md` and the per-pedal docs); the macro's rail clamps cost two Newton ports each |
| DS-1, TS808/9/10, Booster, OD-1, Klon | 1.9-2.9 | 1.0-2.0 | |
| everything else (delays, modulation, plate/spring/hall, pitch, gate, comp) | < 0.5 each | | |
| **worst-case block** (distortion at full gain -> Bassman -> Cab, 20 s of playing, 128-sample blocks) | mean 29.5, p90 40, p99 46, **max 82** (was **497**: a dropout) | | `Tests/ChainSpikeBench.cpp`, `CHAIN_SPIKE=1`. Fixed 2026-09-21 by capping the solver's fallback modes in a runaway and recovering the amp after 8 failed samples instead of 48 (`NodalCircuitSolver.md`, `Bassman5F6A.md`). Cost does not rise with playing level -- the spikes did |
| empty chain (whole app idle) | | | audio thread 0.7%, GUI thread 2.5% |

The user's saved chain (Teste.xml: gate, pitch shift, TS808, Klon, Neural Amp+Cab, analog delay, spring) went from
~27% to ~12% of a core with the two changes below.

## What was done (2026-09-21)
* **`nam_core` tuned for the build machine** (`cmake/NAMCore.cmake`, option `OGMFX_NATIVE_TUNING`, default ON):
  `-march=native -ffast-math` (`-mcpu=native` on ARM). NAM is Eigen matrix products; the default x86-64 baseline has no
  AVX2/FMA. 3.2x faster on a real WaveNet model; `NAMProcessorTests` pass. The binary is now machine-specific
  (`-DOGMFX_NATIVE_TUNING=OFF` for one that must run elsewhere). fast-math is safe here because NAM does no NaN/Inf
  handling of its own; the same flags gave **nothing** for the circuit processors (16.4 vs 16.7% on the Bassman): their
  cost is Newton overhead per sample, not vectorisable arithmetic.
* **Cab IR capped at 150 ms** (`IRLoaderProcessor`, `DynamicCabProcessor`): a 4 s captured file cost 18% of a core; a
  guitar cabinet's response is over in ~100 ms and the rest is a noise floor. Band-by-band level and tone of the capped
  IR against the full one: 0.00 dB in 80 Hz-16 kHz on the Celestion V30. The Reverb role keeps the whole file.
* **`juce::ScopedNoDenormals` in the audio callback** (`AudioEngine`): there was none. Measured effect on this machine
  is small (a few effects' silent tails 0.41 -> 0.28%), but a denormal costs 50-150x a normal float and it is free.

* **BD-2 macro-model gain stages (2026-09-21)**: 7.4 -> 2.6%. **HM-2 one-port transistors**: 6.6 -> 5.4%, then REVERTED as default (a user heard a pop; hard edges peak +1 dB). BD-2 verified against its
  full netlist (`Tests/ReducedOrderEquivalenceTests.cpp`).
* **Bassman**: stayed at ~16.9%. Nothing cheap left: a trajectory-guarded quadratic predictor, compiler flags, sparse folded
  models (only 30% of the entries matter at -80 dB, worth ~5%) and channel-variant preamps (10%, only with a muted channel)
  were measured or costed and none was worth its risk. Its stability got worse before it got better (see its doc).

## What is left, in order of value
1. Bassman-Style Amplifier (16.7%): channel-variant preamps (skip the muted 12AY7 half), cheaper Newton for tube blocks
   (`Bassman5F6A.md`, "Cost").
2. BD-2 and HM-2 (7.4 / 6.5%): reduced-order gain-stage models (`NodalCircuitSolver.md`, "Where the cycles go").
3. A real NAM model chosen for cost: a "standard" WaveNet is the heavy case; NAM's lighter architectures/`Feather`
   captures on TONE3000 run at a fraction of it. Nothing to fix in code; worth knowing when picking models.
4. Lane-parallel `SignalGraph` (roadmap): does not reduce the total, only splits parallel lanes across cores.
