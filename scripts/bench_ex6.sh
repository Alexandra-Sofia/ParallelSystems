#!/bin/bash
set -euo pipefail

make bin/ex6

# shellcheck source=lib.sh
source "$(dirname "$0")/lib.sh"

setup_trap
require_tools
require_program "ex6"

REPEATS=4
RESULTS_DIR="results/ex6"
RESULTS_FILE="$RESULTS_DIR/bench_ex6_results.csv"
SYSTEM_FILE="$RESULTS_DIR/bench_ex6_system.txt"

mkdir -p "$RESULTS_DIR"
collect_system_info "$SYSTEM_FILE"

echo "sweep,size,threads,repeat,sort_time,correctness" > "$RESULTS_FILE"

run_ex6() {
    local sweep="$1" size="$2" mode="$3" threads="$4" repeat="$5"
    local output
    output=$("$BINDIR/ex6" "$size" "$mode" "$threads")

    local sort_time ok
    sort_time=$(echo "$output" | awk '/Sort time/   {print $3}')
    ok=$(echo        "$output" | awk '/Correctness/ {print $2}' | tr -d '[]')

    if [ -z "$sort_time" ] || [ -z "$ok" ]; then
        echo "Error: failed to parse output for size=$size mode=$mode threads=$threads"
        echo "$output"; exit 1
    fi

    echo "$sweep,$size,$threads,$repeat,$sort_time,$ok" >> "$RESULTS_FILE"
    echo "[bench] sweep=$sweep size=$size mode=$mode threads=$threads repeat=$repeat time=$sort_time"
}

echo "[bench] sweep 1: varying threads"
SIZE=10000000
for repeat in $(seq 1 "$REPEATS"); do
    run_ex6 threads "$SIZE" serial 1 "$repeat"
done
for threads in 2 4 8; do
    for repeat in $(seq 1 "$REPEATS"); do
        run_ex6 threads "$SIZE" parallel "$threads" "$repeat"
    done
done

echo "[bench] sweep 2: varying size"
for size in 1000000 10000000 100000000; do
    for repeat in $(seq 1 "$REPEATS"); do
        run_ex6 size "$size" serial 1 "$repeat"
    done
    for repeat in $(seq 1 "$REPEATS"); do
        run_ex6 size "$size" parallel 4 "$repeat"
    done
done

echo "[bench] results written to $RESULTS_FILE"
