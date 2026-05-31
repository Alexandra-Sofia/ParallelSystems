#!/bin/bash
set -euo pipefail

INPUT_FILE="bench_ex1_results.csv"
AVG_FILE="bench_ex1_averages.csv"

if [ ! -f "$INPUT_FILE" ]; then
    echo "Error: $INPUT_FILE not found"
    exit 1
fi

awk -F, '
NR > 1 {
    key = $1 "," $2 "," $3
    serial_sum[key] += $5
    parallel_sum[key] += $6
    speedup_sum[key] += $7
    count[key] += 1
}
END {
    print "degree,mode,threads,avg_serial,avg_parallel,avg_speedup"
    for (key in count) {
        printf "%s,%.6f,%.6f,%.3f\n",
            key,
            serial_sum[key] / count[key],
            parallel_sum[key] / count[key],
            speedup_sum[key] / count[key]
    }
}
' "$INPUT_FILE" | sort -t, -k1,1n -k2,2 -k3,3n > "$AVG_FILE"

echo "[avg] averages written to $AVG_FILE"