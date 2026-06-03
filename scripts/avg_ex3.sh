#!/bin/bash
set -euo pipefail

source "$(dirname "$0")/lib.sh"

RESULTS_DIR="results/ex3"
INPUT_FILE="$RESULTS_DIR/bench_ex3_results.csv"
AVG_FILE="$RESULTS_DIR/bench_ex3_averages.csv"

if [ ! -f "$INPUT_FILE" ]; then
    echo "Error: $INPUT_FILE not found"
    exit 1
fi

avg_csv \
    "$INPUT_FILE" \
    "$AVG_FILE" \
    "1,2,3,4,5,6,7" \
    "9" \
    "sweep,accounts,transactions,scheme,threads,read_pct,read_work_iters,avg_elapsed"

echo "[avg] averages written to $AVG_FILE"