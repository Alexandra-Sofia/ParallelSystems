#!/bin/bash
set -euo pipefail

INPUT_FILE="bench_ex5_results.csv"
AVG_FILE="bench_ex5_averages.csv"

if [ ! -f "$INPUT_FILE" ]; then
    echo "Error: $INPUT_FILE not found"
    exit 1
fi

awk -F, '
NR > 1 {
    key = $1 "," $2 "," $3 "," $4 "," $5

    nnz_sum[key] += $7
    build_s_sum[key] += $8
    build_p_sum[key] += $9
    csr_s_sum[key] += $10
    csr_p_sum[key] += $11
    dense_s_sum[key] += $12
    dense_p_sum[key] += $13
    csr_total_p_sum[key] += $14
    dense_total_p_sum[key] += $15
    speedup_sum[key] += $16
    count[key] += 1

    if ($17 != "OK") {
        failed[key] += 1
    }
}
END {
    print "sweep,size,sparsity,iterations,threads,avg_nnz,avg_csr_build_serial,avg_csr_build_parallel,avg_csr_spmv_serial,avg_csr_spmv_parallel,avg_dense_spmv_serial,avg_dense_spmv_parallel,avg_csr_total_parallel,avg_dense_total_parallel,avg_csr_vs_dense_including_build,correctness"

    for (key in count) {
        status = failed[key] > 0 ? "FAIL" : "OK"

        printf "%s,%.2f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.3f,%s\n",
            key,
            nnz_sum[key] / count[key],
            build_s_sum[key] / count[key],
            build_p_sum[key] / count[key],
            csr_s_sum[key] / count[key],
            csr_p_sum[key] / count[key],
            dense_s_sum[key] / count[key],
            dense_p_sum[key] / count[key],
            csr_total_p_sum[key] / count[key],
            dense_total_p_sum[key] / count[key],
            speedup_sum[key] / count[key],
            status
    }
}
' "$INPUT_FILE" | sort -t, -k1,1 -k2,2n -k3,3n -k4,4n -k5,5n > "$AVG_FILE"

echo "[avg] averages written to $AVG_FILE"