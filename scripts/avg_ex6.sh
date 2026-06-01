#!/bin/bash
set -euo pipefail

INPUT_FILE="bench_ex6_results.csv"
AVG_FILE="bench_ex6_averages.csv"

if [ ! -f "$INPUT_FILE" ]; then
    echo "Error: $INPUT_FILE not found"
    exit 1
fi

awk -F, '
NR > 1 {
    key = $1 "," $2 "," $3
    time_sum[key] += $5
    count[key] += 1

    serial_key = $1
    if ($2 == "serial" && $3 == "1") {
        serial_sum[serial_key] += $5
        serial_count[serial_key] += 1
    }

    if ($6 != "OK") {
        failed[key] += 1
    }
}
END {
    print "size,mode,threads,avg_sort_time,speedup_vs_serial,correctness"

    for (key in count) {
        split(key, parts, ",")
        size = parts[1]

        avg_time = time_sum[key] / count[key]
        if (serial_count[size] > 0) {
            serial_avg = serial_sum[size] / serial_count[size]
            speedup = serial_avg / avg_time
        } else {
            speedup = 0
        }

        status = failed[key] > 0 ? "FAIL" : "OK"

        printf "%s,%.6f,%.3f,%s\n",
            key,
            avg_time,
            speedup,
            status
    }
}
' "$INPUT_FILE" | sort -t, -k1,1n -k2,2 -k3,3n > "$AVG_FILE"

echo "[avg] averages written to $AVG_FILE"