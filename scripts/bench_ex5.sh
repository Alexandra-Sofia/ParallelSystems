#!/bin/bash
set -euo pipefail
# shellcheck source=lib.sh
source "$(dirname "$0")/lib.sh"

setup_trap
require_tools
require_program "ex5"

REPEATS=4
RESULTS_DIR="results/ex5"
RESULTS_FILE="$RESULTS_DIR/bench_ex5_results.csv"
SYSTEM_FILE="$RESULTS_DIR/bench_ex5_system.txt"

mkdir -p "$RESULTS_DIR"
collect_system_info "$SYSTEM_FILE"

echo "sweep,size,sparsity,iterations,threads,repeat,\
csr_build_serial,csr_build_parallel,\
csr_spmv_serial,csr_spmv_parallel,\
dense_spmv_serial,dense_spmv_parallel,correctness" \
    > "$RESULTS_FILE"

run_ex5() {
    local sweep="$1" size="$2" sparsity="$3" iters="$4" threads="$5" repeat="$6"
    local output
    output=$("$BINDIR/ex5" "$size" "$sparsity" "$iters" "$threads")

    local cbs cbp css csp dss dsp ok
    cbs=$(echo "$output" | awk '/CSR build serial/    {print $4}')
    cbp=$(echo "$output" | awk '/CSR build parallel/  {print $4}')
    css=$(echo "$output" | awk '/CSR SpMV serial/     {print $4}')
    csp=$(echo "$output" | awk '/CSR SpMV parallel/   {print $4}')
    dss=$(echo "$output" | awk '/Dense SpMV serial/   {print $4}')
    dsp=$(echo "$output" | awk '/Dense SpMV parallel/ {print $4}')
    ok=$(echo  "$output" | awk '/Correctness/         {print $2}' | tr -d '[]')

    if [ -z "$cbs" ] || [ -z "$ok" ]; then
        echo "Error: failed to parse output for size=$size sparsity=$sparsity threads=$threads"
        echo "$output"; exit 1
    fi

    echo "$sweep,$size,$sparsity,$iters,$threads,$repeat,$cbs,$cbp,$css,$csp,$dss,$dsp,$ok" \
        >> "$RESULTS_FILE"
    echo "[bench] sweep=$sweep size=$size sparsity=$sparsity threads=$threads repeat=$repeat csr_spmv_parallel=$csp"
}

echo "[bench] sweep 1: varying threads"
for threads in 1 2 4 8; do
    for repeat in $(seq 1 "$REPEATS"); do
        run_ex5 threads 2000 90 10 "$threads" "$repeat"
    done
done

echo "[bench] sweep 2: varying sparsity"
for sparsity in 0 50 75 90 99; do
    for repeat in $(seq 1 "$REPEATS"); do
        run_ex5 sparsity 2000 "$sparsity" 10 4 "$repeat"
    done
done

echo "[bench] sweep 3: varying size"
for size in 500 1000 2000 4000; do
    for repeat in $(seq 1 "$REPEATS"); do
        run_ex5 size "$size" 90 10 4 "$repeat"
    done
done

echo "[bench] results written to $RESULTS_FILE"