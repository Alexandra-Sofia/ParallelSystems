#!/bin/bash
set -euo pipefail

trap 'echo "[interrupted] cleaning up..."; pkill -P $$ || true; exit 1' INT TERM

REPEATS=4
PROGRAM="./ex6"
RESULTS_FILE="bench_ex6_results.csv"
SYSTEM_FILE="bench_ex6_system.txt"

SIZES="1000000 5000000 10000000 50000000"
THREADS="1 2 4 8"

if [ ! -x "$PROGRAM" ]; then
    echo "Error: $PROGRAM not found or not executable"
    echo "Run: make ex6"
    exit 1
fi

echo "[bench] collecting system information..."

{
    echo "Hostname:"
    hostname
    echo

    echo "CPU model:"
    lscpu | grep "Model name" | sed 's/^[ \t]*//'
    echo

    echo "CPU cores/threads:"
    lscpu | grep -E "CPU\(s\)|Core\(s\) per socket|Thread\(s\) per core|Socket\(s\)" \
          | sed 's/^[ \t]*//'
    echo

    echo "Operating system:"
    if [ -f /etc/os-release ]; then
        grep PRETTY_NAME /etc/os-release | cut -d= -f2 | tr -d '"'
    else
        uname -a
    fi
    echo

    echo "Kernel:"
    uname -r
    echo

    echo "Compiler:"
    gcc --version | head -n 1
    echo

    echo "Build command:"
    echo "make ex6"
    echo

    echo "Benchmark parameters:"
    echo "Repeats: $REPEATS"
    echo "Sizes: $SIZES"
    echo "Threads: $THREADS"
} > "$SYSTEM_FILE"

echo "size,mode,threads,repeat,sort_time,correctness" > "$RESULTS_FILE"

run_case() {
    local size="$1"
    local mode="$2"
    local threads="$3"
    local repeat="$4"

    local output
    output=$("$PROGRAM" "$size" "$mode" "$threads")

    local sort_time
    local correctness

    sort_time=$(echo "$output" | awk '/Sort time/ {print $3}')
    correctness=$(echo "$output" | awk '/Correctness/ {print $2}' | tr -d '[]')

    if [ -z "$sort_time" ] || [ -z "$correctness" ]; then
        echo "Error: failed to parse output"
        echo "size=$size mode=$mode threads=$threads repeat=$repeat"
        echo "$output"
        exit 1
    fi

    echo "$size,$mode,$threads,$repeat,$sort_time,$correctness" >> "$RESULTS_FILE"

    echo "[bench] size=$size mode=$mode threads=$threads repeat=$repeat time=$sort_time correctness=$correctness"
}

for size in $SIZES; do
    echo "[bench] serial baseline size=$size"

    for repeat in $(seq 1 "$REPEATS"); do
        run_case "$size" "serial" 1 "$repeat"
    done

    echo "[bench] parallel sweep size=$size"

    for threads in $THREADS; do
        for repeat in $(seq 1 "$REPEATS"); do
            run_case "$size" "parallel" "$threads" "$repeat"
        done
    done
done

echo "[bench] results written to $RESULTS_FILE"
echo "[bench] system information written to $SYSTEM_FILE"