#!/usr/bin/env bash
# Phase 6 W6 — on-board benchmark harness for the Cubie A7S.
#
# RUNS ON THE BOARD, on the real OS image (or stock Radxa OS after
# scripts/setup-target.sh) — never in the cross toolchain and never under
# qemu, whose wall-clock numbers are meaningless for audio work.
#
# Getting the pieces onto the board is a manual step:
#   scp build-aarch64/Tests/OpenGuitarMultiFx_Tests  ogmfx@<board>:/tmp/
#   scp scripts/bench-target.sh                      ogmfx@<board>:/tmp/
# then on the board:
#   /tmp/bench-target.sh /tmp/OpenGuitarMultiFx_Tests
#
# Optional env (same convention as docs/CpuCostMap.md):
#   BENCH_NAM=/path/model.nam   real NAM model for EffectCostBench
#   BENCH_IR=/path/cab.wav      real cab IR for EffectCostBench
#   CYCLICTEST_ARGS="-m -p 80 -i 1000 -D 60"   override cyclictest run
#
# Output: a markdown block on stdout, ready to paste under "Results" in
# docs/arm-benchmarks.md. Raw bench logs go to stderr-adjacent files in
# ./bench-out-<ts>/ so the markdown stays pasteable.
set -euo pipefail

TESTS_BIN="${1:-}"
if [ -z "$TESTS_BIN" ] || [ ! -x "$TESTS_BIN" ]; then
    echo "usage: $0 <path-to-OpenGuitarMultiFx_Tests>" >&2
    echo "  (cross-build it first: scripts/cross-build.sh aarch64, then scp to the board)" >&2
    exit 2
fi

OUT_DIR="bench-out-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT_DIR"

soc_temp() {  # max of all thermal zones, in °C
    local t max=0
    for z in /sys/class/thermal/thermal_zone*/temp; do
        [ -r "$z" ] || continue
        t=$(( $(cat "$z") / 1000 ))
        [ "$t" -gt "$max" ] && max=$t
    done
    echo "$max"
}

governor() { cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo "?"; }

echo "== bench-target.sh: $OUT_DIR" >&2
echo "== EffectCostBench (EFFECT_BENCH=1)..." >&2
EFFECT_BENCH=1 "$TESTS_BIN" EffectCostBench > "$OUT_DIR/effect-cost-bench.log" 2>&1 || \
    echo "   !! EffectCostBench exited nonzero — see $OUT_DIR/effect-cost-bench.log" >&2

echo "== ChainSpikeBench (CHAIN_SPIKE=1)..." >&2
CHAIN_SPIKE=1 "$TESTS_BIN" ChainSpikeBench > "$OUT_DIR/chain-spike-bench.log" 2>&1 || \
    echo "   !! ChainSpikeBench exited nonzero — see $OUT_DIR/chain-spike-bench.log" >&2

CYCLICTEST_ARGS="${CYCLICTEST_ARGS:--m -p 80 -i 1000 -D 60}"
CYCLICTEST_LOG=""
if command -v cyclictest >/dev/null 2>&1; then
    echo "== cyclictest $CYCLICTEST_ARGS ..." >&2
    # shellcheck disable=SC2086
    cyclictest $CYCLICTEST_ARGS > "$OUT_DIR/cyclictest.log" 2>&1 || \
        echo "   !! cyclictest exited nonzero — see $OUT_DIR/cyclictest.log" >&2
    CYCLICTEST_LOG="$OUT_DIR/cyclictest.log"
else
    echo "== cyclictest not installed — skipped (apt install rt-tests on the board)" >&2
fi

echo "== done. Markdown block below; paste it into docs/arm-benchmarks.md under 'Results'." >&2

# ---------------------------------------------------------------- markdown ---
cat <<EOF

### On-board run — $(date -u +"%Y-%m-%d %H:%M UTC")

| Field | Value |
|---|---|
| Kernel | \`$(uname -r)\` |
| Governor | \`$(governor)\` |
| Binary | \`$(basename "$TESTS_BIN")\` — cross-built with \`OGMFX_TARGET_CPU=cortex-a76\` |
| SoC temp (end) | $(soc_temp) °C |
| BENCH_NAM | \`${BENCH_NAM:-<none>}\` |
| BENCH_IR | \`${BENCH_IR:-<none>}\` |

<details><summary>EffectCostBench (EFFECT_BENCH=1)</summary>

\`\`\`
$(cat "$OUT_DIR/effect-cost-bench.log")
\`\`\`

</details>

<details><summary>ChainSpikeBench (CHAIN_SPIKE=1)</summary>

\`\`\`
$(cat "$OUT_DIR/chain-spike-bench.log")
\`\`\`

</details>
EOF

if [ -n "$CYCLICTEST_LOG" ]; then
    # cyclictest's T: lines carry per-thread min/avg/max; the trailing
    # "Max Latencies" line is the headline number for the scenario table.
    cat <<EOF

#### cyclictest \`$CYCLICTEST_ARGS\`

\`\`\`
$(grep -E "^#|Max Latencies|Min Latencies|Avg Latencies" "$CYCLICTEST_LOG" || tail -40 "$CYCLICTEST_LOG")
\`\`\`
EOF
else
    echo
    echo "#### cyclictest"
    echo
    echo "_SKIPPED — \`cyclictest\` not installed on the board (\`apt install rt-tests\`)._"
fi

echo
echo "_Fill the scenario matrix above from these logs; raw output kept in \`$OUT_DIR/\`._"
