#!/bin/bash
set -euo pipefail
# shellcheck source=lib.sh
source "$(dirname "$0")/lib.sh"

# sweep(1) scheme(2) threads(3) read_pct(4) read_work_iters(5) repeat(6) elapsed(7) correctness(8)
avg_csv \
    "results/ex3/bench_ex3_results.csv" \
    "results/ex3/bench_ex3_averages.csv" \
    "1,2,3,4,5" \
    "7" \
    "sweep,scheme,threads,read_pct,read_work_iters,avg_elapsed"
