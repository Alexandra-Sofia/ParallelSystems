#!/bin/bash
set -euo pipefail
# shellcheck source=lib.sh
source "$(dirname "$0")/lib.sh"

# sweep(1) accounts(2) scheme(3) threads(4) read_pct(5) read_work_iters(6)
# repeat(7) elapsed(8) correctness(9)
avg_csv \
    "results/ex3/bench_ex3_results.csv" \
    "results/ex3/bench_ex3_averages.csv" \
    "1,2,3,4,5,6" \
    "8" \
    "sweep,accounts,scheme,threads,read_pct,read_work_iters,avg_elapsed"