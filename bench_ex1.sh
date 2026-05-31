#!/bin/bash
set -euo pipefail

trap 'echo "[interrupted] cleaning up..."; pkill -P $$ || true; exit 1' INT TERM

REPEATS=4
DEGREES="10000 100000 500000"
THREADS="1 2 4 8"
MODES="pthreads openmp"
PROGRAM="./ex1"
RESULTS_FILE="bench_ex1_results.csv"
SYSTEM_FILE="bench_ex1_system.txt"

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
    echo "gcc -O3 -Wall -Wextra -pthread -fopenmp ex1.c -o ex1"
    echo

    echo "Benchmark parameters:"
    echo "Repeats: $REPEATS"
    echo "Degrees: $DEGREES"
    echo "Threads: $THREADS"
    echo "Modes: $MODES"
} > "$SYSTEM_FILE"

echo "degree,mode,threads,repeat,serial_time,parallel_time,speedup,correctness" > "$RESULTS_FILE"

for degree in $DEGREES; do
    echo "[bench] degree=$degree"

    for threads in $THREADS; do
        for mode in $MODES; do
            for repeat in $(seq 1 "$REPEATS"); do
                output=$("$PROGRAM" "$degree" "$mode" "$threads")

                serial_time=$(echo "$output" | awk '/Serial time/ {print $3}')
                parallel_time=$(echo "$output" | awk '/Parallel time/ {print $3}')
                correctness=$(echo "$output" | awk '/Correctness/ {print $2}' | tr -d '[]')

                if [ -z "$serial_time" ] || [ -z "$parallel_time" ] || [ -z "$correctness" ]; then
                    echo "Error: failed to parse output for degree=$degree mode=$mode threads=$threads repeat=$repeat"
                    echo "$output"
                    exit 1
                fi

                if [ "$parallel_time" = "0" ] || [ "$parallel_time" = "0.000000" ]; then
                    speedup="NA"
                else
                    speedup=$(echo "scale=3; $serial_time / $parallel_time" | bc -l)
                fi

                echo "$degree,$mode,$threads,$repeat,$serial_time,$parallel_time,$speedup,$correctness" >> "$RESULTS_FILE"

                echo "[bench] degree=$degree mode=$mode threads=$threads repeat=$repeat serial=$serial_time parallel=$parallel_time speedup=$speedup correctness=$correctness"
            done
        done
    done
done

echo "[bench] results written to $RESULTS_FILE"
echo "[bench] system information written to $SYSTEM_FILE"