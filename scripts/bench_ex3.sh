#!/bin/bash
set -euo pipefail
# shellcheck source=lib.sh
source "$(dirname "$0")/lib.sh"

setup_trap
require_tools
require_program "ex3"

REPEATS=4
ACCOUNTS=1000
TRANSACTIONS=10000
THREADS="1 2 4 8"
SCHEMES="coarse_mutex fine_mutex coarse_rw fine_rw"
RESULTS_DIR="results/ex3"
RESULTS_FILE="$RESULTS_DIR/bench_ex3_results.csv"
SYSTEM_FILE="$RESULTS_DIR/bench_ex3_system.txt"

mkdir -p "$RESULTS_DIR"
collect_system_info "$SYSTEM_FILE"

echo "sweep,scheme,threads,read_pct,read_work_iters,repeat,elapsed,correctness" \
    > "$RESULTS_FILE"

run_ex3() {
    local sweep="$1" scheme="$2" threads="$3" read_pct="$4" work="$5" repeat="$6"
    local output
    output=$("$BINDIR/ex3" "$ACCOUNTS" "$TRANSACTIONS" "$read_pct" \
             "$scheme" "$threads" "$work")

    local elapsed ok
    elapsed=$(echo "$output" | awk '/Elapsed/ {print $2}')
    ok=$(echo      "$output" | awk '/Correctness/ {print $2}' | tr -d '[]')

    if [ -z "$elapsed" ] || [ -z "$ok" ]; then
        echo "Error: failed to parse output for scheme=$scheme threads=$threads read_pct=$read_pct"
        echo "$output"; exit 1
    fi

    echo "$sweep,$scheme,$threads,$read_pct,$work,$repeat,$elapsed,$ok" \
        >> "$RESULTS_FILE"
    echo "[bench] sweep=$sweep scheme=$scheme threads=$threads read_pct=$read_pct work=$work repeat=$repeat elapsed=$elapsed"
}

echo "[bench] sweep 1: varying read_pct"
for read_pct in 0 20 50 80 95; do
    for scheme in $SCHEMES; do
        for repeat in $(seq 1 "$REPEATS"); do
            run_ex3 read_pct "$scheme" 4 "$read_pct" 100 "$repeat"
        done
    done
done

echo "[bench] sweep 2: varying threads"
for threads in $THREADS; do
    for scheme in $SCHEMES; do
        for repeat in $(seq 1 "$REPEATS"); do
            run_ex3 threads "$scheme" "$threads" 80 100 "$repeat"
        done
    done
done

echo "[bench] sweep 3: varying read_work_iters"
for work in 1 10 100 500; do
    for scheme in $SCHEMES; do
        for repeat in $(seq 1 "$REPEATS"); do
            run_ex3 read_work "$scheme" 4 80 "$work" "$repeat"
        done
    done
done

echo "[bench] results written to $RESULTS_FILE"
