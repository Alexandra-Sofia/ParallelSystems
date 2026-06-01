#!/bin/bash
set -euo pipefail

trap 'echo "[interrupted] cleaning up..."; pkill -P $$ || true; exit 1' INT TERM

REPEATS=4
THREADS="1 2 4 8"
ITERATIONS="100000 1000000 10000000"
MODES="mutex rwlock atomic"
PROGRAM="./ex2"
RESULTS_FILE="bench_ex2_results.csv"
SYSTEM_FILE="bench_ex2_system.txt"

if [ ! -x "$PROGRAM" ]; then
    echo "Error: $PROGRAM not found or not executable"
    exit 1
fi

if ! command -v bc >/dev/null 2>&1; then
    echo "Error: bc is required but not installed"
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
    lscpu | grep -E "CPU\(s\)|Core\(s\) per socket|Thread\(s\) per core|Socket\(s\)" | sed 's/^[ \t]*//'
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

    echo "Compile command used:"
    echo "gcc -O3 -Wall -Wextra -pthread ex2.c -o ex2"
    echo

    echo "Benchmark parameters:"
    echo "Repeats: $REPEATS"
    echo "Threads: $THREADS"
    echo "Iterations: $ITERATIONS"
    echo "Modes: $MODES"
} > "$SYSTEM_FILE"

echo "threads,iterations,mode,repeat,elapsed,correctness" > "$RESULTS_FILE"

for threads in $THREADS; do
    for iterations in $ITERATIONS; do
        for mode in $MODES; do
            for repeat in $(seq 1 "$REPEATS"); do
                output=$("$PROGRAM" "$threads" "$iterations" "$mode")

                elapsed=$(echo "$output" | awk '/Elapsed/ {print $2}')
                correctness=$(echo "$output" | awk '/Correctness/ {print $2}' | tr -d '[]')

                if [ -z "$elapsed" ] || [ -z "$correctness" ]; then
                    echo "Error: failed to parse output for threads=$threads iterations=$iterations mode=$mode repeat=$repeat"
                    echo "$output"
                    exit 1
                fi

                echo "$threads,$iterations,$mode,$repeat,$elapsed,$correctness" >> "$RESULTS_FILE"

                echo "[bench] threads=$threads iterations=$iterations mode=$mode repeat=$repeat elapsed=$elapsed correctness=$correctness"
            done
        done
    done
done

echo "[bench] results written to $RESULTS_FILE"
echo "[bench] system information written to $SYSTEM_FILE"