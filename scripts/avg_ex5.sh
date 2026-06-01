#!/bin/bash
set -euo pipefail
# shellcheck source=lib.sh
source "$(dirname "$0")/lib.sh"

# sweep(1) size(2) sparsity(3) iterations(4) threads(5) repeat(6)
# csr_build_serial(7) csr_build_parallel(8)
# csr_spmv_serial(9) csr_spmv_parallel(10)
# dense_spmv_serial(11) dense_spmv_parallel(12)
# csr_total_parallel(13) csr_vs_dense_including_build(14) correctness(15)
avg_csv     "results/ex5/bench_ex5_results.csv"     "results/ex5/bench_ex5_averages.csv"     "1,2,3,4,5"     "7,8,9,10,11,12,13,14"     "sweep,size,sparsity,iterations,threads,avg_csr_build_serial,avg_csr_build_parallel,avg_csr_spmv_serial,avg_csr_spmv_parallel,avg_dense_spmv_serial,avg_dense_spmv_parallel,avg_csr_total_parallel,avg_csr_vs_dense_including_build"
