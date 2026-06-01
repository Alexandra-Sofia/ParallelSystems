#!/bin/bash
set -euo pipefail

make bin/ex1

# shellcheck source=lib.sh
source "$(dirname "$0")/lib.sh"

setup_trap
require_tools
require_program "ex1"

REPEATS=4
DEGREES="10000 100000 500000"
THREADS="1 2 4 8"
MODES="pthreads openmp"
RESULTS_DIR="results/ex1"
RESULTS_FILE="$RESULTS_DIR/bench_ex1_results.csv"
SYSTEM_FILE="$RESULTS_DIR/bench_ex1_system.txt"

mkdir -p "$RESULTS_DIR"
collect_system_info "$SYSTEM_FILE"

echo "degree,mode,threads,repeat,serial_time,parallel_time,speedup,correctness" \
    > "$RESULTS_FILE"

for degree in $DEGREES; do
    echo "[bench] degree=$degree"
    for threads in $THREADS; do
        for mode in $MODES; do
            for repeat in $(seq 1 "$REPEATS"); do
                output=$("$BINDIR/ex1" "$degree" "$mode" "$threads")

                serial=$(echo   "$output" | awk '/Serial time/  {print $3}')
                parallel=$(echo "$output" | awk '/Parallel time/ {print $3}')
                speedup=$(echo  "$output" | awk '/Speedup/       {print $2}' \
                          | tr -d 'x')
                ok=$(echo       "$output" | awk '/Correctness/   {print $2}' \
                     | tr -d '[]')

                if [ -z "$serial" ] || [ -z "$parallel" ] || [ -z "$ok" ]; then
                    echo "Error: failed to parse output for degree=$degree mode=$mode threads=$threads"
                    echo "$output"; exit 1
                fi

                echo "$degree,$mode,$threads,$repeat,$serial,$parallel,$speedup,$ok" \
                    >> "$RESULTS_FILE"
                echo "[bench] degree=$degree mode=$mode threads=$threads repeat=$repeat parallel=$parallel speedup=${speedup}x"
            done
        done
    done
done

echo "[bench] results written to $RESULTS_FILE"
