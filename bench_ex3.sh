#!/bin/bash
set -euo pipefail

trap 'echo "[interrupted] cleaning up..."; pkill -P $$ || true; exit 1' INT TERM

REPEATS=4
PROGRAM="./ex3"
RESULTS_FILE="bench_ex3_results.csv"
SYSTEM_FILE="bench_ex3_system.txt"

DEFAULT_ACCOUNTS=1000
DEFAULT_TRANSACTIONS=10000
DEFAULT_THREADS=4
DEFAULT_READ_PCT=80
DEFAULT_READ_WORK=100

THREADS="1 2 4 8"
SCHEMES="coarse_mutex fine_mutex coarse_rw fine_rw"

if [ ! -x "$PROGRAM" ]; then
    echo "Error: $PROGRAM not found or not executable"
    echo "Run: make ex3"
    exit 1
fi

echo "[bench] collecting system information..."

{
    echo "Hostname:"
    hostname
    echo

    echo "CPU model:"
    lscpu | grep "Model name" | sed 's/^[ \t]*//'
    echo

    echo "CPU cores/threads:"
    lscpu | grep -E "CPU\(s\)|Core\(s\) per socket|Thread\(s\) per core|Socket\(s\)" \
          | sed 's/^[ \t]*//'
    echo

    echo "Operating system:"
    if [ -f /etc/os-release ]; then
        grep PRETTY_NAME /etc/os-release | cut -d= -f2 | tr -d '"'
    else
        uname -a
    fi
    echo

    echo "Kernel:"
    uname -r
    echo

    echo "Compiler:"
    gcc --version | head -n 1
    echo

    echo "Build command:"
    echo "make ex3"
    echo

    echo "Benchmark parameters:"
    echo "Repeats: $REPEATS"
    echo "Default accounts: $DEFAULT_ACCOUNTS"
    echo "Default transactions per thread: $DEFAULT_TRANSACTIONS"
    echo "Default threads: $DEFAULT_THREADS"
    echo "Default read pct: $DEFAULT_READ_PCT"
    echo "Default read work iters: $DEFAULT_READ_WORK"
    echo "Schemes: $SCHEMES"
} > "$SYSTEM_FILE"

echo "sweep,accounts,transactions,scheme,threads,read_pct,read_work_iters,repeat,elapsed,correctness" \
    > "$RESULTS_FILE"

run_case() {
    local sweep="$1"
    local accounts="$2"
    local transactions="$3"
    local scheme="$4"
    local threads="$5"
    local read_pct="$6"
    local read_work="$7"
    local repeat="$8"

    local output
    output=$("$PROGRAM" "$accounts" "$transactions" "$read_pct" "$scheme" "$threads" "$read_work")

    local elapsed
    local correctness

    elapsed=$(echo "$output" | awk '/Elapsed/ {print $2}')
    correctness=$(echo "$output" | awk '/Correctness/ {print $2}' | tr -d '[]')

    if [ -z "$elapsed" ] || [ -z "$correctness" ]; then
        echo "Error: failed to parse output"
        echo "sweep=$sweep accounts=$accounts transactions=$transactions scheme=$scheme threads=$threads read_pct=$read_pct read_work=$read_work repeat=$repeat"
        echo "$output"
        exit 1
    fi

    echo "$sweep,$accounts,$transactions,$scheme,$threads,$read_pct,$read_work,$repeat,$elapsed,$correctness" \
        >> "$RESULTS_FILE"

    echo "[bench] sweep=$sweep accounts=$accounts txns=$transactions scheme=$scheme threads=$threads read_pct=$read_pct work=$read_work repeat=$repeat elapsed=$elapsed correctness=$correctness"
}

echo "[bench] sweep 1: varying read_pct"

for read_pct in 0 20 50 80 95; do
    for scheme in $SCHEMES; do
        for repeat in $(seq 1 "$REPEATS"); do
            run_case "read_pct" \
                "$DEFAULT_ACCOUNTS" \
                "$DEFAULT_TRANSACTIONS" \
                "$scheme" \
                "$DEFAULT_THREADS" \
                "$read_pct" \
                "$DEFAULT_READ_WORK" \
                "$repeat"
        done
    done
done

echo "[bench] sweep 2: varying threads"

for threads in $THREADS; do
    for scheme in $SCHEMES; do
        for repeat in $(seq 1 "$REPEATS"); do
            run_case "threads" \
                "$DEFAULT_ACCOUNTS" \
                "$DEFAULT_TRANSACTIONS" \
                "$scheme" \
                "$threads" \
                "$DEFAULT_READ_PCT" \
                "$DEFAULT_READ_WORK" \
                "$repeat"
        done
    done
done

echo "[bench] sweep 3: varying read_work_iters"

for read_work in 1 10 100 500; do
    for scheme in $SCHEMES; do
        for repeat in $(seq 1 "$REPEATS"); do
            run_case "read_work" \
                "$DEFAULT_ACCOUNTS" \
                "$DEFAULT_TRANSACTIONS" \
                "$scheme" \
                "$DEFAULT_THREADS" \
                "$DEFAULT_READ_PCT" \
                "$read_work" \
                "$repeat"
        done
    done
done

echo "[bench] sweep 4: varying accounts"

for accounts in 100 1000 10000; do
    for scheme in $SCHEMES; do
        for repeat in $(seq 1 "$REPEATS"); do
            run_case "accounts" \
                "$accounts" \
                "$DEFAULT_TRANSACTIONS" \
                "$scheme" \
                "$DEFAULT_THREADS" \
                "$DEFAULT_READ_PCT" \
                "$DEFAULT_READ_WORK" \
                "$repeat"
        done
    done
done

echo "[bench] sweep 5: varying transactions per thread"

for transactions in 1000 10000 100000; do
    for scheme in $SCHEMES; do
        for repeat in $(seq 1 "$REPEATS"); do
            run_case "transactions" \
                "$DEFAULT_ACCOUNTS" \
                "$transactions" \
                "$scheme" \
                "$DEFAULT_THREADS" \
                "$DEFAULT_READ_PCT" \
                "$DEFAULT_READ_WORK" \
                "$repeat"
        done
    done
done

echo "[bench] results written to $RESULTS_FILE"
echo "[bench] system information written to $SYSTEM_FILE"