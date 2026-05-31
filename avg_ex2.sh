#!/bin/bash
set -euo pipefail

INPUT_FILE="bench_ex2_results.csv"
AVG_FILE="bench_ex2_averages.csv"

if [ ! -f "$INPUT_FILE" ]; then
    echo "Error: $INPUT_FILE not found"
    exit 1
fi

awk -F, '
NR > 1 {
    key = $1 "," $2 "," $3
    elapsed_sum[key] += $5
    count[key] += 1

    if ($6 != "OK") {
        failed[key] += 1
    }
}
END {
    print "threads,iterations,mode,avg_elapsed,correctness"
    for (key in count) {
        status = failed[key] > 0 ? "FAIL" : "OK"
        printf "%s,%.6f,%s\n",
            key,
            elapsed_sum[key] / count[key],
            status
    }
}
' "$INPUT_FILE" | sort -t, -k2,2n -k1,1n -k3,3 > "$AVG_FILE"

echo "[avg] averages written to $AVG_FILE"