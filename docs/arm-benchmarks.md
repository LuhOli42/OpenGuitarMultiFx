# ARM benchmarks — Radxa Cubie A7S

**STATUS: PENDING HARDWARE.** This document is the skeleton for the on-board numbers; every table
below is empty until someone runs `scripts/bench-target.sh` on the real A7S and pastes its output
here. Numbers taken under qemu-aarch64 are explicitly *not* valid — emulation skews wall-clock
measurements far beyond any threshold we care about.

Targets come from `ARCHITECTURE.md` §G (project targets to validate empirically, not literature
values). Per-effect cost reference on the x86 dev bench: `docs/CpuCostMap.md`.

## Test setup (fill in on first run)

| Field | Value |
|---|---|
| Board | Radxa Cubie A7S (Allwinner A733, 2×Cortex-A76 + 6×A55) |
| OS image | _PENDING_ (image/ build, or stock Radxa OS + `scripts/setup-target.sh`) |
| Kernel | _PENDING_ (`uname -r` — BSP 5.15.x-a733 expected; PREEMPT_RT unconfirmed) |
| Governor | _PENDING_ (`performance` expected after setup-target.sh) |
| Sample rate / block | 48 kHz / 128 samples (2.67 ms) — also try 64 if budget allows |
| Audio path | USB class-2 interface (Phase 6 decision #3) |
| Build | `scripts/cross-build.sh aarch64` (`-DOGMFX_TARGET_CPU=cortex-a76 -ffast-math` on `nam_core`) |

## How to run

On the board, with the cross-built `OpenGuitarMultiFx_Tests` binary and this repo's
`scripts/bench-target.sh` (script prints a ready-to-paste block — see its header for details):

```sh
./scripts/bench-target.sh /path/to/OpenGuitarMultiFx_Tests
# optionally with real model files:
BENCH_NAM=/path/to/model.nam BENCH_IR=/path/to/cab.wav \
    ./scripts/bench-target.sh /path/to/OpenGuitarMultiFx_Tests
```

The on-board run is a manual step: whoever has the board runs it. Paste the emitted block under
"Results" below.

## Scenario matrix (ARCHITECTURE.md §G)

Round-trip = measured input→output latency including the driver (ms). SoC temp = max
`thermal_zone*` reading at end of the 30-min run (°C). CPU% is % of one A76 core unless noted.

| # | Scenario | CPU target | CPU% measured | Xruns (target 0 / 30 min) | Round-trip (ms) | SoC temp (°C) | Pass? |
|---|---|---|---|---|---|---|---|
| 1 | NAM alone | < 15% | _PENDING_ | _PENDING_ | _PENDING_ | _PENDING_ | _PENDING_ |
| 2 | NAM + IR (cab) | < 25% | _PENDING_ | _PENDING_ | _PENDING_ | _PENDING_ | _PENDING_ |
| 3 | NAM + 1 pedal | < 30% | _PENDING_ | _PENDING_ | _PENDING_ | _PENDING_ | _PENDING_ |
| 4 | Multiple pedals (gate+comp+drive+eq) | < 40% | _PENDING_ | _PENDING_ | _PENDING_ | _PENDING_ | _PENDING_ |
| 5 | Two amps in parallel (split/merge) | < 55% | _PENDING_ | _PENDING_ | _PENDING_ | _PENDING_ | _PENDING_ |
| 6 | Full chain | < 70% | _PENDING_ | _PENDING_ | _PENDING_ | _PENDING_ | _PENDING_ |

## Cyclictest (kernel RT character)

The open risk from `docs/Phase6Plan.md`: PREEMPT_RT on the A733 BSP kernel is unconfirmed — these
numbers decide whether 64-sample blocks are viable or we ship 128.

| Run | Command | Min (µs) | Avg (µs) | Max (µs) | Notes |
|---|---|---|---|---|---|
| idle | `cyclictest -m -p 80 -i 1000 -D 60` | _PENDING_ | _PENDING_ | _PENDING_ | |
| under audio load | same, during scenario 6 | _PENDING_ | _PENDING_ | _PENDING_ | |
| memory/IO stress | same, under `stress-ng` | _PENDING_ | _PENDING_ | _PENDING_ | optional |

## Results

_PASTE `scripts/bench-target.sh` OUTPUT BLOCKS HERE — PENDING HARDWARE._
