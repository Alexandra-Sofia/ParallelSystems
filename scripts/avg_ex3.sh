#!/bin/bash
set -euo pipefail
# shellcheck source=lib.sh
source "$(dirname "$0")/lib.sh"

# sweep(1) accounts(2) transactions(3) scheme(4) threads(5)
# read_pct(6) read_work_iters(7) repeat(8) elapsed(9) correctness(10)
avg_csv     "results/ex3/bench_ex3_results.csv"     "results/ex3/bench_ex3_averages.csv"     "1,2,3,4,5,6,7"     "9"     "sweep,accounts,transactions,scheme,threads,read_pct,read_work_iters,avg_elapsed"
