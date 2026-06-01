#!/bin/bash
set -euo pipefail

trap 'echo "[interrupted] cleaning up..."; pkill -P $$ || true; exit 1' INT TERM

REPEATS=4
PROGRAM="./ex5"
RESULTS_FILE="bench_ex5_results.csv"
SYSTEM_FILE="bench_ex5_system.txt"

DEFAULT_SIZE=2000
DEFAULT_SPARSITY=90
DEFAULT_ITERATIONS=10
DEFAULT_THREADS=4

THREADS="1 2 4 8"
SIZES="1000 2000 4000"
SPARSITIES="0 50 75 90 95 99"
ITERATIONS_LIST="1 5 10 20"

if [ ! -x "$PROGRAM" ]; then
    echo "Error: $PROGRAM not found or not executable"
    echo "Run: make ex5"
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
    echo "make ex5"
    echo

    echo "Benchmark parameters:"
    echo "Repeats: $REPEATS"
    echo "Default size: $DEFAULT_SIZE"
    echo "Default sparsity: $DEFAULT_SPARSITY"
    echo "Default iterations: $DEFAULT_ITERATIONS"
    echo "Default threads: $DEFAULT_THREADS"
    echo "Thread sweep: $THREADS"
    echo "Size sweep: $SIZES"
    echo "Sparsity sweep: $SPARSITIES"
    echo "Iteration sweep: $ITERATIONS_LIST"
} > "$SYSTEM_FILE"

echo "sweep,size,sparsity,iterations,threads,repeat,nnz,csr_build_serial,csr_build_parallel,csr_spmv_serial,csr_spmv_parallel,dense_spmv_serial,dense_spmv_parallel,csr_total_parallel,dense_total_parallel,csr_vs_dense_including_build,correctness" \
    > "$RESULTS_FILE"

run_case() {
    local sweep="$1"
    local size="$2"
    local sparsity="$3"
    local iterations="$4"
    local threads="$5"
    local repeat="$6"

    local output
    output=$("$PROGRAM" "$size" "$sparsity" "$iterations" "$threads")

    local nnz
    local csr_build_serial
    local csr_build_parallel
    local csr_spmv_serial
    local csr_spmv_parallel
    local dense_spmv_serial
    local dense_spmv_parallel
    local correctness

    nnz=$(echo "$output" | awk '/NNZ:/ {print $2}')
    csr_build_serial=$(echo "$output" | awk '/CSR build serial/ {print $4}')
    csr_build_parallel=$(echo "$output" | awk '/CSR build parallel/ {print $4}')
    csr_spmv_serial=$(echo "$output" | awk '/CSR SpMV serial/ {print $4}')
    csr_spmv_parallel=$(echo "$output" | awk '/CSR SpMV parallel/ {print $4}')
    dense_spmv_serial=$(echo "$output" | awk '/Dense SpMV serial/ {print $4}')
    dense_spmv_parallel=$(echo "$output" | awk '/Dense SpMV parallel/ {print $4}')
    correctness=$(echo "$output" | awk '/Correctness/ {print $2}' | tr -d '[]')

    if [ -z "$nnz" ] || [ -z "$csr_build_serial" ] || \
       [ -z "$csr_build_parallel" ] || [ -z "$csr_spmv_serial" ] || \
       [ -z "$csr_spmv_parallel" ] || [ -z "$dense_spmv_serial" ] || \
       [ -z "$dense_spmv_parallel" ] || [ -z "$correctness" ]; then
        echo "Error: failed to parse output"
        echo "sweep=$sweep size=$size sparsity=$sparsity iterations=$iterations threads=$threads repeat=$repeat"
        echo "$output"
        exit 1
    fi

    local csr_total_parallel
    local dense_total_parallel
    local csr_vs_dense_including_build

    csr_total_parallel=$(awk -v a="$csr_build_parallel" -v b="$csr_spmv_parallel" \
        'BEGIN { printf "%.6f", a + b }')

    dense_total_parallel="$dense_spmv_parallel"

    csr_vs_dense_including_build=$(awk -v d="$dense_total_parallel" -v c="$csr_total_parallel" \
        'BEGIN {
            if (c == 0) {
                print "NA"
            } else {
                printf "%.3f", d / c
            }
        }')

    echo "$sweep,$size,$sparsity,$iterations,$threads,$repeat,$nnz,$csr_build_serial,$csr_build_parallel,$csr_spmv_serial,$csr_spmv_parallel,$dense_spmv_serial,$dense_spmv_parallel,$csr_total_parallel,$dense_total_parallel,$csr_vs_dense_including_build,$correctness" \
        >> "$RESULTS_FILE"

    echo "[bench] sweep=$sweep size=$size sparsity=$sparsity iterations=$iterations threads=$threads repeat=$repeat nnz=$nnz csr_total=$csr_total_parallel dense=$dense_total_parallel speedup=$csr_vs_dense_including_build correctness=$correctness"
}

echo "[bench] sweep 1: varying threads"

for threads in $THREADS; do
    for repeat in $(seq 1 "$REPEATS"); do
        run_case "threads" \
            "$DEFAULT_SIZE" \
            "$DEFAULT_SPARSITY" \
            "$DEFAULT_ITERATIONS" \
            "$threads" \
            "$repeat"
    done
done

echo "[bench] sweep 2: varying matrix size"

for size in $SIZES; do
    for repeat in $(seq 1 "$REPEATS"); do
        run_case "size" \
            "$size" \
            "$DEFAULT_SPARSITY" \
            "$DEFAULT_ITERATIONS" \
            "$DEFAULT_THREADS" \
            "$repeat"
    done
done

echo "[bench] sweep 3: varying sparsity"

for sparsity in $SPARSITIES; do
    for repeat in $(seq 1 "$REPEATS"); do
        run_case "sparsity" \
            "$DEFAULT_SIZE" \
            "$sparsity" \
            "$DEFAULT_ITERATIONS" \
            "$DEFAULT_THREADS" \
            "$repeat"
    done
done

echo "[bench] sweep 4: varying SpMV iterations"

for iterations in $ITERATIONS_LIST; do
    for repeat in $(seq 1 "$REPEATS"); do
        run_case "iterations" \
            "$DEFAULT_SIZE" \
            "$DEFAULT_SPARSITY" \
            "$iterations" \
            "$DEFAULT_THREADS" \
            "$repeat"
    done
done

echo "[bench] results written to $RESULTS_FILE"
echo "[bench] system information written to $SYSTEM_FILE"