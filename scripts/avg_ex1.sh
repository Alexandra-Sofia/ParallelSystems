#!/bin/bash
set -euo pipefail
# shellcheck source=lib.sh
source "$(dirname "$0")/lib.sh"

# Columns: degree(1) mode(2) threads(3) repeat(4) serial(5) parallel(6) speedup(7) correctness(8)
avg_csv \
    "results/ex1/bench_ex1_results.csv" \
    "results/ex1/bench_ex1_averages.csv" \
    "1,2,3" \
    "5,6,7" \
    "degree,mode,threads,avg_serial,avg_parallel,avg_speedup"
