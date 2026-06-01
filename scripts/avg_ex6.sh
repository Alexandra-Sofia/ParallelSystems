#!/bin/bash
set -euo pipefail
# shellcheck source=lib.sh
source "$(dirname "$0")/lib.sh"

# Columns: sweep(1) size(2) threads(3) repeat(4) sort_time(5) correctness(6)
avg_csv \
    "results/ex6/bench_ex6_results.csv" \
    "results/ex6/bench_ex6_averages.csv" \
    "1,2,3" \
    "5" \
    "sweep,size,threads,avg_sort_time"
