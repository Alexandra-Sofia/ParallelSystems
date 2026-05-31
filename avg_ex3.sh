#!/bin/bash
set -euo pipefail

INPUT_FILE="bench_ex3_results.csv"
AVG_FILE="bench_ex3_averages.csv"

if [ ! -f "$INPUT_FILE" ]; then
    echo "Error: $INPUT_FILE not found"
    exit 1
fi

awk -F, '
NR > 1 {
    key = $1 "," $2 "," $3 "," $4 "," $5 "," $6 "," $7
    elapsed_sum[key] += $9
    count[key] += 1

    if ($10 != "OK") {
        failed[key] += 1
    }
}
END {
    print "sweep,accounts,transactions,scheme,threads,read_pct,read_work_iters,avg_elapsed,correctness"
    for (key in count) {
        status = failed[key] > 0 ? "FAIL" : "OK"
        printf "%s,%.6f,%s\n",
            key,
            elapsed_sum[key] / count[key],
            status
    }
}
' "$INPUT_FILE" | sort -t, -k1,1 -k2,2n -k3,3n -k4,4 -k5,5n -k6,6n -k7,7n > "$AVG_FILE"

echo "[avg] averages written to $AVG_FILE"