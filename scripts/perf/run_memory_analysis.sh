#!/usr/bin/env bash
# run_memory_analysis.sh — memory analysis for a quicX binary via valgrind.
#
# Modes (env MODE=...):
#   massif  (default) heap profiling over time  -> massif.out.<pid> + massif.txt
#   memcheck          leaks + invalid accesses  -> memcheck.log
#
# Usage:
#   scripts/perf/run_memory_analysis.sh [binary]     # default: build/bin/performance_benchmark
# Env:
#   MODE=massif|memcheck   BIN args can be passed via ARGS="..."
set -euo pipefail

BIN="${1:-build/bin/performance_benchmark}"
MODE="${MODE:-massif}"
ARGS="${ARGS:-}"

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

if [ ! -x "$BIN" ]; then
    echo "error: binary not found or not executable: $BIN" >&2
    exit 1
fi

if ! command -v valgrind >/dev/null 2>&1; then
    echo "error: valgrind not found" >&2
    echo "install valgrind, or run an ASan/LSan-instrumented build of the binary instead" >&2
    exit 1
fi

case "$MODE" in
    massif)
        echo "[massif] profiling heap of $BIN ..."
        valgrind --tool=massif --stacks=yes --time-unit=B -- "$BIN" $ARGS
        OUT=$(ls -t massif.out.* 2>/dev/null | head -1 || true)
        if [ -n "$OUT" ]; then
            ms_print "$OUT" > massif.txt
            echo "done: $OUT (human-readable: massif.txt)"
        fi
        ;;
    memcheck)
        echo "[memcheck] leak + memory-error check of $BIN ..."
        valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
                 --log-file=memcheck.log -- "$BIN" $ARGS
        echo "done: memcheck.log"
        ;;
    *)
        echo "error: unknown MODE '$MODE' (expected massif|memcheck)" >&2
        exit 1
        ;;
esac
