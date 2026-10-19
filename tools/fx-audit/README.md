# fx-audit — offline effect audit harness

Render every registered `EffectProcessor` offline over a guitar DI and probe
inputs, then measure what changed. Built for the Oct-2026 full-catalog audit;
use it to verify voicing/calibration fixes reproducibly.

## Inputs (bundled)

- `probes/` — synthetic inputs: `probe-sine*.wav` (82/110/220/440/1000 Hz for
  THD + even/odd ratio), `probe-sweep.wav` (30 Hz–18 kHz log sweep for FR),
  `probe-clicks.wav` (impulse train for echo/reverb tails), `probe-noise.wav`.
- `ir/cab-4x12.wav` — small synthetic 4x12 cab IR for `*+Cab` chains
  (amps render raw power-amp output without it).
- `metrics.json` — measured results, refreshed 2026-10-11 (see
  `audit-2026-10-11.md` for the delta vs the Oct-2026 baseline;
  `music` = per-effect band deltas vs DI, `probes` = THD/FR/echo metrics).
- Guitar DI comes from `Tests/fixtures/di/guitar-di.wav` (override `DI_WAV`).

## Run

```sh
# build the renderer once (repo root)
cmake --build build --target OpenGuitarMultiFx_RenderFx -j2

# one-shot render
build/Tests/OpenGuitarMultiFx_RenderFx_artefacts/Release/OpenGuitarMultiFx_RenderFx \
    Tests/fixtures/di/guitar-di.wav /tmp/out.wav <EffectKey> param=value --quality=high

# list every effect key + its params/ranges/defaults
.../OpenGuitarMultiFx_RenderFx --list

# batch renders + probes + metrics
FX_AUDIT_DIR=/tmp/fx-audit python3 tools/fx-audit/run_renders.py
FX_AUDIT_DIR=/tmp/fx-audit python3 tools/fx-audit/run_probes.py
FX_AUDIT_DIR=/tmp/fx-audit python3 tools/fx-audit/analyze.py   # writes metrics.json

# drive-swept probe: variant discrimination (see caveat below)
FX_AUDIT_DIR=/tmp/fx-audit python3 tools/fx-audit/run_drive_sweep.py \
    --update-metrics=/tmp/fx-audit/metrics.json

# CPU table -- only on an IDLE box (a concurrent analyze.py inflates rows 10-25 pp)
BENCH_CSV=/tmp/fx-audit/cpu-bench.csv python3 tools/fx-audit/run_cpu_bench.py

# delta vs the tracked baseline (run BEFORE publishing over it); CPU_CONTROLS =
# keys whose code did not change, used to normalise for box speed
CPU_CONTROLS=KeyA,KeyB python3 tools/fx-audit/delta_report.py \
    tools/fx-audit/metrics.json /tmp/fx-audit/metrics.json \
    tools/fx-audit/cpu-bench.csv /tmp/fx-audit/cpu-bench.csv > delta.md

# publish: the fresh run's metrics replace the tracked baseline
cp /tmp/fx-audit/metrics.json tools/fx-audit/metrics.json
cp /tmp/fx-audit/cpu-bench.csv tools/fx-audit/cpu-bench.csv
```

`run_renders.py` / `run_probes.py` append every key from `RenderFx --list`
that is not in their hand-tuned lists, so new effects are always covered.

`analyze.py` writes fresh `music`/`probes` under `FX_AUDIT_DIR`, so merging
the sweep into the tracked `metrics.json` directly would leave its music and
probe results stale — merge into the audit-dir file, then copy it over.

Deps: `numpy scipy soundfile` (`pip install` if missing). Env overrides:
`RENDERFX_BIN`, `FX_AUDIT_DIR` (outputs), `DI_WAV`, `CAB_IR`, `NAM_FIXTURE`,
`PROBE_WAV_DIR`, plus `DRIVE_POINTS` / `SWEEP_KEYS` for the sweep.

## Reading the numbers

- `band_delta_db`: 7 log-spaced bands, dB vs the same input unprocessed —
  the effect's "EQ signature". THD/even-odd from sine probes; `echo_lags` and
  `tail_rms` from clicks. Flat ≈0dB everywhere = passthrough/clean.
- `drive_sweep` (metrics.json): per-variant measurements across the main
  gain control's travel (0.1–0.9), `results[key].settings[drive]` →
  THD/even-odd/rms/band-delta per setting, plus a `divergence` summary
  (max pairwise spread per family + the drive where it occurs).

## Caveat: fixed-drive probes hide variant differences

A single-point probe at high drive can measure distinct circuits as
"identical": variants converge at deep clipping (the real pedals do too),
and the registry's unity trims normalize the internal level differences the
metrics then look for. PR #17 hit exactly this — TS808/TS9/TS10 and
RAT/RAT2/TurboRAT measured "nearly identical" at `*=0.85` while the circuits
provably differ (clip knee, turbo diode set, level trims). **The drive-swept
probe is the discriminating tool**: TurboRAT vs RAT2 shows 0.99% vs 25.28%
THD at drive 0.1 (an 18 dB band-delta spread), where the fixed 0.85 point
showed them equal.
