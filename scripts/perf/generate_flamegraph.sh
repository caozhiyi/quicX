#!/usr/bin/env bash
# generate_flamegraph.sh — generate a CPU flame graph for a quicX binary.
#
# Two paths, auto-selected:
#   A) Linux `perf` + FlameGraph's flamegraph.pl (preferred on hosts).
#   B) In-repo SIGPROF sampling profiler under test/perf/tools/ — for
#      restricted containers without perf (see
#      docs/internal/perf_flamegraph_analysis.md for its design).
#
# Usage:
#   scripts/perf/generate_flamegraph.sh [binary]      # default: build/bin/performance_benchmark
# Env:
#   DURATION=10            sampling seconds
#   FREQ=99                sampling frequency (Hz)
#   OUT=flamegraph         output basename (.perf/.stacks/.svg/.collapsed)
#   FLAMEGRAPH_DIR=~/FlameGraph   where flamegraph.pl lives (path A)
#   PROFILER_BIN=          path to the built sampling-profiler driver (path B)
set -euo pipefail

BIN="${1:-build/bin/performance_benchmark}"
DURATION="${DURATION:-10}"
FREQ="${FREQ:-99}"
OUT="${OUT:-flamegraph}"

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

if [ ! -x "$BIN" ]; then
    echo "error: binary not found or not executable: $BIN" >&2
    echo "build it first, or pass the path as the first argument" >&2
    exit 1
fi

# ---------- Path A: perf + FlameGraph ----------
if command -v perf >/dev/null 2>&1 && perf stat -e task-clock true >/dev/null 2>&1; then
    echo "[path A] sampling $BIN for ${DURATION}s at ${FREQ}Hz via perf ..."
    perf record -F "$FREQ" -g -o "$OUT.perf" -- "$BIN" &
    PERF_PID=$!
    sleep "$DURATION"
    kill -INT "$PERF_PID" 2>/dev/null || true
    wait "$PERF_PID" 2>/dev/null || true

    perf script -i "$OUT.perf" > "$OUT.stacks"
    FG="${FLAMEGRAPH_DIR:-$HOME/FlameGraph}/flamegraph.pl"
    if [ -x "$FG" ]; then
        "$FG" --title "quicX CPU flame graph" "$OUT.stacks" > "$OUT.svg"
        echo "done: $OUT.svg (raw stacks: $OUT.stacks)"
    else
        echo "stacks saved to $OUT.stacks"
        echo "flamegraph.pl not found — clone https://github.com/brendangregg/FlameGraph"
        echo "and set FLAMEGRAPH_DIR=<clone dir>, then run:"
        echo "  \$FLAMEGRAPH_DIR/flamegraph.pl $OUT.stacks > $OUT.svg"
    fi
    exit 0
fi

# ---------- Path B: in-repo SIGPROF profiler ----------
PROFILER_BIN="${PROFILER_BIN:-build/bin/profile_decode_packets}"
if [ -x "$PROFILER_BIN" ]; then
    echo "[path B] sampling $PROFILER_BIN (in-repo SIGPROF profiler) ..."
    "$PROFILER_BIN"
    echo "profiler dumps collapsed stacks + /proc/self/maps to its output files;"
    echo "symbolize with test/perf/tools/resolve_stacks.py, then render with flamegraph.pl"
    exit 0
fi

echo "no sampler available:" >&2
echo "  - perf missing/blocked (path A), and profiler driver not built (path B)" >&2
echo "to use path B, build the driver from test/perf/tools/ (see" >&2
echo "docs/internal/perf_flamegraph_analysis.md) and set PROFILER_BIN=<binary>" >&2
exit 2
