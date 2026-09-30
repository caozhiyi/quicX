#!/usr/bin/env bash
# perf_regression.sh — run a quicX benchmark several times and compare the
# median against a recorded baseline; exit non-zero on regression beyond a
# tolerance. Intended for CI (see .github/workflows usage) and local checks.
#
# Usage:
#   scripts/ci/perf_regression.sh [benchmark_binary]
#
# Env / defaults:
#   BASELINE=scripts/ci/perf_baseline.txt   baseline file, "<name> <value>" lines
#   RUNS=5                                  repetitions (median is compared)
#   TOLERANCE_PCT=10                        allowed slowdown (%)
#   METRIC_LINE='^BM_|bench|_test'          only output lines matching this are parsed
#   METRIC_NAME='s/[[:space:]].*//'         sed expr to extract the benchmark name
#   METRIC_VALUE (fixed)                    last number on the line is the value
#   INVERT=0                                0: smaller is better; 1: bigger is better
#
# Create the baseline with --update: it (re)records medians into $BASELINE.
set -euo pipefail

BIN="${1:-build/bin/quicx_benchmark}"
BASELINE="${BASELINE:-$(cd "$(dirname "$0")" && pwd)/perf_baseline.txt}"
RUNS="${RUNS:-5}"
TOLERANCE_PCT="${TOLERANCE_PCT:-10}"
METRIC_LINE="${METRIC_LINE:-^BM_|bench|_test}"
METRIC_NAME="${METRIC_NAME:-s/[[:space:]].*//}"
INVERT="${INVERT:-0}"

if [ ! -x "$BIN" ]; then
    echo "error: benchmark binary not found or not executable: $BIN" >&2
    exit 1
fi

run_once() {
    "$BIN" 2>/dev/null | grep -E "$METRIC_LINE" \
        | sed -E "$METRIC_NAME" \
        | paste -d' ' - <("$BIN" 2>/dev/null | grep -E "$METRIC_LINE" \
                           | grep -oE '[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?' \
                           | tail -1)
}

# name -> median over RUNS runs
collect() {
    for _ in $(seq "$RUNS"); do
        "$BIN" 2>/dev/null | grep -E "$METRIC_LINE" | while IFS= read -r line; do
            name=$(printf '%s' "$line" | sed -E "$METRIC_NAME")
            value=$(printf '%s' "$line" | grep -oE '[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?' | tail -1)
            [ -n "$name" ] && [ -n "$value" ] && printf '%s %s\n' "$name" "$value"
        done
    done | sort -k1,1 -g | awk '
        { v[$1]=v[$1] " " $2; n[$1]++ }
        END { for (k in v) { split(v[k], a, " "); print k, a[int((n[k]+1)/2)] } }
    ' | sort
}

RESULTS=$(collect)

if [ "${1:-}" = "--update" ] || [ "${UPDATE:-0}" = "1" ]; then
    echo "$RESULTS" > "$BASELINE"
    echo "baseline updated: $BASELINE"
    exit 0
fi

if [ ! -f "$BASELINE" ]; then
    echo "error: baseline not found: $BASELINE (create it with --update)" >&2
    exit 1
fi

status=0
while read -r name median; do
    [ -n "$name" ] || continue
    base=$(awk -v n="$name" '$1 == n { print $2; exit }' "$BASELINE")
    if [ -z "$base" ]; then
        echo "SKIP  $name (not in baseline)"
        continue
    fi
    if [ "$INVERT" = "1" ]; then
        # bigger is better: regression when median < base * (1 - tol/100)
        worse=$(awk -v m="$median" -v b="$base" -v t="$TOLERANCE_PCT" \
                'BEGIN { print (m < b * (1 - t/100)) ? 1 : 0 }')
    else
        # smaller is better: regression when median > base * (1 + tol/100)
        worse=$(awk -v m="$median" -v b="$base" -v t="$TOLERANCE_PCT" \
                'BEGIN { print (m > b * (1 + t/100)) ? 1 : 0 }')
    fi
    delta=$(awk -v m="$median" -v b="$base" 'BEGIN { printf "%+.1f", (m - b) / b * 100 }')
    if [ "$worse" = "1" ]; then
        echo "REGRESS $name baseline=$base median=$median (${delta}% > ${TOLERANCE_PCT}%)"
        status=1
    else
        echo "OK     $name baseline=$base median=$median (${delta}%)"
    fi
done <<< "$RESULTS"

exit $status
