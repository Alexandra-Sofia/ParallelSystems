#!/bin/bash
set -euo pipefail

INPUT_FILE="bench_ex4_results.csv"
AVG_FILE="bench_ex4_averages.csv"

if [ ! -f "$INPUT_FILE" ]; then
    echo "Error: $INPUT_FILE not found"
    exit 1
fi

awk -F, '
NR > 1 {
    key = $1 "," $2 "," $3
    elapsed_sum[key] += $5
    throughput_sum[key] += $6
    count[key] += 1
}
END {
    print "threads,iterations,mode,avg_elapsed,avg_throughput_mpasses_per_sec"
    for (key in count) {
        printf "%s,%.6f,%.6f\n",
            key,
            elapsed_sum[key] / count[key],
            throughput_sum[key] / count[key]
    }
}
' "$INPUT_FILE" | sort -t, -k1,1n -k2,2n -k3,3 > "$AVG_FILE"

echo "[avg] averages written to $AVG_FILE"