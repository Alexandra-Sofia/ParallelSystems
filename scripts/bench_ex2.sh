#!/bin/bash
set -euo pipefail

make bin/ex2

# shellcheck source=lib.sh
source "$(dirname "$0")/lib.sh"

setup_trap
require_tools
require_program "ex2"

REPEATS=4
THREADS="1 2 4 8"
ITERATIONS="100000 1000000 10000000"
MODES="mutex rwlock atomic"
RESULTS_DIR="results/ex2"
RESULTS_FILE="$RESULTS_DIR/bench_ex2_results.csv"
SYSTEM_FILE="$RESULTS_DIR/bench_ex2_system.txt"

mkdir -p "$RESULTS_DIR"
collect_system_info "$SYSTEM_FILE"

echo "mode,threads,iterations,repeat,elapsed,correctness" > "$RESULTS_FILE"

for iterations in $ITERATIONS; do
    echo "[bench] iterations=$iterations"
    for threads in $THREADS; do
        for mode in $MODES; do
            for repeat in $(seq 1 "$REPEATS"); do
                output=$("$BINDIR/ex2" "$threads" "$iterations" "$mode")

                elapsed=$(echo "$output" | awk '/Elapsed/     {print $2}')
                ok=$(echo      "$output" | awk '/Correctness/ {print $2}' \
                     | tr -d '[]')

                if [ -z "$elapsed" ] || [ -z "$ok" ]; then
                    echo "Error: failed to parse output for mode=$mode threads=$threads iterations=$iterations"
                    echo "$output"; exit 1
                fi

                echo "$mode,$threads,$iterations,$repeat,$elapsed,$ok" \
                    >> "$RESULTS_FILE"
                echo "[bench] mode=$mode threads=$threads iterations=$iterations repeat=$repeat elapsed=$elapsed"
            done
        done
    done
done

echo "[bench] results written to $RESULTS_FILE"
