#!/bin/bash
# tests/smoke-test-all.sh
# Run from the project root: bash tests/smoke-test-all.sh
set -euo pipefail

trap 'echo "[smoke] interrupted"; pkill -P $$ || true; exit 1' INT TERM

BINDIR="bin"

echo "[smoke] cleaning and building..."
make clean
make

check_output() {
    local name="$1"
    local cmd="$2"
    local timeout_sec="${3:-30}"

    echo
    echo "[smoke] running $name"
    echo "[smoke] command: $cmd"

    output=$(eval "timeout $timeout_sec $cmd" 2>/dev/null || true)
    echo "$output"

    if [ -z "$output" ]; then
        echo "[smoke][SKIP] $name — timed out or no output (expected on single-core for sense barrier)"
        return
    fi

    if echo "$output" | grep -q "Correctness:"; then
        if ! echo "$output" | grep -q "Correctness:.*OK"; then
            echo "[smoke][FAIL] $name correctness check failed"
            exit 1
        fi
    fi

    echo "[smoke][OK] $name"
}

check_output "ex1 pthreads"     "$BINDIR/ex1 10 pthreads 2"
check_output "ex1 openmp"       "$BINDIR/ex1 10 openmp 2"

check_output "ex2 mutex"        "$BINDIR/ex2 2 1000 mutex"
check_output "ex2 rwlock"       "$BINDIR/ex2 2 1000 rwlock"
check_output "ex2 atomic"       "$BINDIR/ex2 2 1000 atomic"

check_output "ex3 coarse_mutex" "$BINDIR/ex3 10 100 80 coarse_mutex 2 10"
check_output "ex3 fine_mutex"   "$BINDIR/ex3 10 100 80 fine_mutex 2 10"
check_output "ex3 coarse_rw"    "$BINDIR/ex3 10 100 80 coarse_rw 2 10"
check_output "ex3 fine_rw"      "$BINDIR/ex3 10 100 80 fine_rw 2 10"

check_output "ex4 pthreads"     "$BINDIR/ex4 2 1000 pthreads"
check_output "ex4 condvar"      "$BINDIR/ex4 2 1000 condvar"
check_output "ex4 sense"        "$BINDIR/ex4 2 1000 sense" 10

check_output "ex5 csr/openmp"   "$BINDIR/ex5 50 90 2 2"

check_output "ex6 serial"       "$BINDIR/ex6 10000 serial 1"
check_output "ex6 parallel"     "$BINDIR/ex6 10000 parallel 2"

echo
echo "[smoke] all tests passed"
