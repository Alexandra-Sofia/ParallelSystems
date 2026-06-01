#!/bin/bash
set -euo pipefail
# shellcheck source=lib.sh
source "$(dirname "$0")/lib.sh"

# Columns: mode(1) threads(2) iterations(3) repeat(4) elapsed(5) correctness(6)
avg_csv \
    "results/ex2/bench_ex2_results.csv" \
    "results/ex2/bench_ex2_averages.csv" \
    "1,2,3" \
    "5" \
    "mode,threads,iterations,avg_elapsed"
