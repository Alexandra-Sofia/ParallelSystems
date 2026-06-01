#!/bin/bash
set -euo pipefail

trap 'echo "[interrupted] cleaning up..."; pkill -P $$ || true; exit 1' INT TERM

REPEATS=4
PROGRAM="./ex4"
RESULTS_FILE="bench_ex4_results.csv"
SYSTEM_FILE="bench_ex4_system.txt"

THREADS="1 2 4 8 16"
ITERATIONS="10000 100000 1000000"
MODES="pthreads condvar sense"

if [ ! -x "$PROGRAM" ]; then
    echo "Error: $PROGRAM not found or not executable"
    echo "Run: make ex4"
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
    echo "make ex4"
    echo

    echo "Benchmark parameters:"
    echo "Repeats: $REPEATS"
    echo "Threads: $THREADS"
    echo "Iterations: $ITERATIONS"
    echo "Modes: $MODES"
} > "$SYSTEM_FILE"

echo "threads,iterations,mode,repeat,elapsed,throughput_mpasses_per_sec" > "$RESULTS_FILE"

for threads in $THREADS; do
    for iterations in $ITERATIONS; do
        for mode in $MODES; do
            for repeat in $(seq 1 "$REPEATS"); do
                output=$("$PROGRAM" "$threads" "$iterations" "$mode")

                elapsed=$(echo "$output" | awk '/Elapsed/ {print $2}')
                throughput=$(echo "$output" | awk '/Throughput/ {print $2}')

                if [ -z "$elapsed" ] || [ -z "$throughput" ]; then
                    echo "Error: failed to parse output"
                    echo "threads=$threads iterations=$iterations mode=$mode repeat=$repeat"
                    echo "$output"
                    exit 1
                fi

                echo "$threads,$iterations,$mode,$repeat,$elapsed,$throughput" >> "$RESULTS_FILE"

                echo "[bench] threads=$threads iterations=$iterations mode=$mode repeat=$repeat elapsed=$elapsed throughput=$throughput"
            done
        done
    done
done

echo "[bench] results written to $RESULTS_FILE"
echo "[bench] system information written to $SYSTEM_FILE"