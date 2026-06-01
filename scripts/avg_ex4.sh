#!/bin/bash
set -euo pipefail
# shellcheck source=lib.sh
source "$(dirname "$0")/lib.sh"

# mode(1) threads(2) iterations(3) repeat(4) elapsed(5) throughput(6)
avg_csv \
    "results/ex4/bench_ex4_results.csv" \
    "results/ex4/bench_ex4_averages.csv" \
    "1,2,3" \
    "5,6" \
    "mode,threads,iterations,avg_elapsed,avg_throughput"
