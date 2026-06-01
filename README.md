# Handout1 — Programming with Pthreads & OpenMP

**Course:** Parallel Computing Systems (M127)  
**Institution:** Department of Informatics & Telecommunications, University of Athens  
**Academic Year:** 2025–2026

---

## Table of Contents

- [Overview](#overview)
- [Repository Structure](#repository-structure)
- [Prerequisites](#prerequisites)
- [Building](#building)
- [Running the Smoke Test](#running-the-smoke-test)
- [Exercises](#exercises)
  - [Ex 1.1 — Polynomial Multiplication](#ex-11--polynomial-multiplication)
  - [Ex 1.2 — Shared Counter](#ex-12--shared-counter)
  - [Ex 1.3 — Bank Simulation](#ex-13--bank-simulation)
  - [Ex 1.4 — Barrier Implementations](#ex-14--barrier-implementations)
  - [Ex 1.5 — Sparse Matrix-Vector Multiplication](#ex-15--sparse-matrix-vector-multiplication)
  - [Ex 1.6 — Parallel Mergesort](#ex-16--parallel-mergesort)
- [Benchmarking](#benchmarking)
- [Averaging Results](#averaging-results)
- [Output Format](#output-format)
- [Design Notes](#design-notes)

---

## Overview

This repository contains the implementation for Handout1 of the Parallel Computing Systems course. It covers six exercises in parallel programming using **Pthreads** and **OpenMP** in C, along with a structured benchmarking and result-averaging pipeline.

All programs are written in C11, compiled with GCC, and target the department's Linux cluster (`linux01.di.uoa.gr` through `linux30.di.uoa.gr`).

---

## Repository Structure

```
hw1_final/
├── Makefile                  — builds all executables into bin/
├── README.md                 — this file
├── src/                      — C source files, one per exercise
│   ├── ex1.c
│   ├── ex2.c
│   ├── ex3.c
│   ├── ex4.c
│   ├── ex5.c
│   └── ex6.c
├── bin/                      — compiled executables (created by make, not versioned)
│   ├── ex1 … ex6
├── scripts/                  — benchmarking and averaging scripts
│   ├── lib.sh                — shared functions sourced by all scripts
│   ├── bench_ex1.sh … bench_ex6.sh
│   └── avg_ex1.sh … avg_ex6.sh
├── tests/
│   └── smoke-test-all.sh     — quick correctness check for all exercises
├── results/                  — created at runtime by bench scripts, not versioned
│   └── exN/
│       ├── bench_exN_results.csv
│       ├── bench_exN_averages.csv
│       └── bench_exN_system.txt
└── reports/                  — report documents, one subfolder per exercise
    └── exN/
```

> `bin/` and `results/` are created at runtime and are not included in the repository.

---

## Prerequisites

| Requirement | Minimum version | Notes |
|-------------|----------------|-------|
| GCC | 9.0 | Must support `-fopenmp` and C11 atomics |
| GNU Make | 4.0 | |
| `bc` | any | Used by bench scripts for floating-point averaging |
| `awk`, `sort` | any | Standard POSIX tools |

On the department machines all prerequisites are available by default.

---

## Building

All commands must be run from the **project root** (the directory containing `Makefile`).

```bash
# Build all six executables into bin/
make

# Remove all compiled binaries
make clean

# Rebuild from scratch
make clean && make
```

The Makefile compiles with `-O2 -Wall -Wextra -Wpedantic -std=c11 -fopenmp` and links against `-lpthread -lm`. Zero warnings are expected on GCC 13.

---

## Running the Smoke Test

The smoke test builds all executables and runs each with minimal parameters to verify correctness before committing to a full benchmark run.

```bash
bash tests/smoke-test-all.sh
```

Expected output ends with:

```
[smoke] all tests passed
```

> **Note on the sense-reversal barrier (ex4):** the smoke test applies a 10-second timeout to the sense barrier case. On single-core machines the spin-based barrier cannot make progress and the test will print `[SKIP]` for that case. On the multi-core department machines it completes correctly.

---

## Exercises

All programs are invoked from the project root using the `bin/` prefix. All programs print timing information to **stdout** and progress information to **stderr**. Benchmark scripts capture only stdout so progress lines do not pollute result files.

Exit codes follow a consistent convention across all exercises:

| Code | Meaning |
|------|---------|
| `0` | Success |
| `1` | Invalid arguments |
| `2` | Correctness check failed |

---

### Ex 1.1 — Polynomial Multiplication

Multiplies two random dense polynomials of degree `n` using a serial O(n²) algorithm and a parallel algorithm (Pthreads or OpenMP). Verifies that both produce identical results.

```
Usage: bin/ex1 <degree> <pthreads|openmp> <num_threads>
```

```bash
# Example
bin/ex1 100000 pthreads 4
bin/ex1 100000 openmp 4
```

**Key design decisions:**
- Output coefficients are partitioned across threads using **cyclic distribution** to balance load — computing `result[k]` requires `k+1` multiplications for small k, peaking at the midpoint and tapering back. Block distribution would give the middle threads all the heavy work; cyclic gives each thread a balanced mix.
- No synchronisation is needed: each output coefficient `k` is owned by exactly one thread.
- OpenMP uses `schedule(static, 1)` to match the cyclic pattern of the Pthreads implementation.

---

### Ex 1.2 — Shared Counter

All threads increment a shared counter in a tight loop. Three synchronisation approaches are compared: mutex, read-write lock, and GCC atomic fetch-and-add.

```
Usage: bin/ex2 <num_threads> <iterations> <mutex|rwlock|atomic>
```

```bash
bin/ex2 4 1000000 mutex
bin/ex2 4 1000000 rwlock
bin/ex2 4 1000000 atomic
```

**Key design decisions:**
- `__atomic_fetch_add` with `__ATOMIC_SEQ_CST` maps to a single hardware instruction (LOCK XADD on x86), avoiding OS-level lock overhead entirely.
- The rwlock is used in write mode for every operation. Since there are no concurrent readers, it provides no advantage over a mutex and incurs additional bookkeeping overhead, making it consistently the slowest of the three.

---

### Ex 1.3 — Bank Simulation

Simulates concurrent bank transactions (money transfers and balance queries) on a shared account array. Four locking schemes are compared across two granularities and two lock types.

```
Usage: bin/ex3 <num_accounts> <transactions_per_thread> <read_pct>
               <coarse_mutex|fine_mutex|coarse_rw|fine_rw>
               <num_threads> <read_work_iters>
```

```bash
# 80% reads, fine-grained rwlock, extended read section
bin/ex3 1000 10000 80 fine_rw 4 100
```

| Parameter | Description |
|-----------|-------------|
| `read_pct` | Percentage of transactions that are balance queries `[0, 100]` |
| `read_work_iters` | Number of `sqrt` iterations inside the read critical section; controls its duration |

**Key design decisions:**
- Fine-grained locks always acquired in **ascending account index order** to prevent deadlock when two threads attempt a transfer between the same pair of accounts in opposite directions.
- `read_work_iters` makes the rwlock advantage observable: with trivially short read sections the overhead of rwlock bookkeeping dominates. With longer read sections, the ability for multiple readers to proceed concurrently outweighs that overhead.
- Correctness is verified by checking that the total sum of all account balances is unchanged after all transactions complete.

---

### Ex 1.4 — Barrier Implementations

Three reusable barrier implementations are benchmarked: the POSIX library barrier, a condvar-based barrier, and a sense-reversal centralised barrier.

```
Usage: bin/ex4 <num_threads> <iterations> <pthreads|condvar|sense>
```

```bash
bin/ex4 8 1000000 pthreads
bin/ex4 8 1000000 condvar
bin/ex4 8 1000000 sense
```

**Key design decisions:**

| Implementation | Mechanism | Reusable | Busy-wait |
|---------------|-----------|----------|-----------|
| `pthreads` | `pthread_barrier_t` | Yes | OS-dependent |
| `condvar` | mutex + condition variable + phase flip | Yes | No |
| `sense` | atomic countdown + thread-local sense flag | Yes | Yes |

- The condvar barrier's **phase flip** (alternating between 0 and 1 each pass) is what makes it reusable without a reset step: threads in the next pass wait for the new phase value, which is unambiguous from the previous one.
- The sense-reversal barrier uses `__atomic_sub_fetch` with `__ATOMIC_ACQ_REL` on the arrival counter, eliminating the need for a mutex in the critical path entirely.
- The sense barrier is faster than condvar when threads ≤ cores and barrier waits are short. When threads > cores, spinning is harmful: waiting threads consume CPU time that the last arriving thread needs to reach the barrier. The benchmark includes 16 threads to demonstrate this regime.

---

### Ex 1.5 — Sparse Matrix-Vector Multiplication

Constructs a random sparse matrix in CSR (Compressed Sparse Row) format and performs repeated SpMV iterations. Four timing paths are measured: serial and parallel CSR build, serial and parallel SpMV, with a dense baseline for comparison.

```
Usage: bin/ex5 <matrix_size> <sparsity_pct> <iterations> <num_threads>
```

```bash
# 2000×2000 matrix, 90% zeros, 10 SpMV iterations, 4 threads
bin/ex5 2000 90 10 4
```

| Parameter | Description |
|-----------|-------------|
| `sparsity_pct` | Percentage of zero elements `[0, 99]` |
| `iterations` | Number of SpMV repetitions; each iteration's output is the next iteration's input |

**CSR format:**

| Array | Size | Content |
|-------|------|---------|
| `values` | nnz | Non-zero element values |
| `col_idx` | nnz | Column index of each non-zero |
| `row_ptr` | n+1 | `row_ptr[i]` = start of row i in values/col_idx |

**Key design decisions:**
- CSR construction uses a **two-pass parallel approach**: count nnz per row in parallel, compute `row_ptr` via serial prefix sum (O(n), negligible), fill `values` and `col_idx` in parallel.
- The serial prefix sum step is intentional: a parallel prefix scan would require a reduction tree and synchronisation barriers, with negligible benefit for the sizes benchmarked.
- SpMV is trivially parallel — each row is an independent dot product with no shared writes.
- All vectors are `long long` to prevent overflow during iterative multiplication.

---

### Ex 1.6 — Parallel Mergesort

Top-down recursive mergesort parallelised with OpenMP tasks. A cutoff threshold prevents task creation overhead from dominating on small subarrays.

```
Usage: bin/ex6 <array_size> <serial|parallel> <num_threads>
```

```bash
bin/ex6 10000000 serial 1
bin/ex6 10000000 parallel 4
```

**Key design decisions:**
- A single auxiliary array (`aux`) is allocated once and passed through all recursive calls, avoiding O(n log n) heap allocations across merge steps.
- `#pragma omp single` around the initial recursive call ensures only one thread spawns the top-level tasks; without it, every thread in the parallel region would independently start sorting the full array.
- The `if()` clause on `#pragma omp task` lets the OpenMP runtime execute small subarrays inline rather than spawning a task, which would have higher overhead than the sort itself.
- The default cutoff is `10000` elements. Below this threshold the overhead of task creation exceeds the parallelism benefit and the serial path is taken.

---

## Benchmarking

All bench scripts must be run from the **project root**. Results are written to `results/exN/` which is created automatically on first run.

```bash
# Run all benchmarks (one at a time — do not run concurrently on shared machines)
bash scripts/bench_ex1.sh
bash scripts/bench_ex2.sh
bash scripts/bench_ex3.sh
bash scripts/bench_ex4.sh
bash scripts/bench_ex5.sh
bash scripts/bench_ex6.sh
```

Each script:
1. Verifies the corresponding binary exists in `bin/`
2. Collects system information (hostname, CPU model, core count, OS, kernel, compiler) into `bench_exN_system.txt`
3. Runs the experiment grid with `REPEATS=4` repetitions per configuration
4. Writes one raw CSV row per repetition to `bench_exN_results.csv`

> **Important:** Run benchmarks during off-peak hours on the department machines. The `linux0X` machines are shared; load from other users will skew timing results. If a single run looks like an outlier, discard it and rerun.

### Benchmark parameters

| Exercise | Key sweep variables |
|----------|-------------------|
| ex1 | degree × threads × mode (pthreads, openmp) |
| ex2 | iterations × threads × mode (mutex, rwlock, atomic) |
| ex3 | read_pct, threads, read_work_iters × scheme |
| ex4 | iterations × threads × mode (pthreads, condvar, sense) |
| ex5 | threads, sparsity, matrix size |
| ex6 | array size × threads |

---

## Averaging Results

After each bench script completes, run the corresponding averaging script to collapse the raw per-repeat rows into a single averaged row per configuration:

```bash
bash scripts/avg_ex1.sh
bash scripts/avg_ex2.sh
bash scripts/avg_ex3.sh
bash scripts/avg_ex4.sh
bash scripts/avg_ex5.sh
bash scripts/avg_ex6.sh
```

Each script writes `bench_exN_averages.csv` to the same `results/exN/` directory.

---

## Output Format

### stdout (captured by bench scripts)

Each program prints labelled fields on separate lines, for example:

```
Serial time:   0.341200 s
Parallel time: 0.087300 s  [pthreads, 4 threads]
Speedup:       3.91x
Correctness:   [OK]
```

### stderr (progress, not captured)

```
[ex1] degree=100000 mode=pthreads threads=4
[ex1] running serial multiply...
[ex1] serial done (0.341200 s)
[ex1] running parallel multiply...
[ex1] parallel done (0.087300 s)
[ex1] verifying results...
```

### CSV result files

Raw results contain one row per repeat. Example for ex1:

```csv
degree,mode,threads,repeat,serial_time,parallel_time,speedup,correctness
100000,pthreads,4,1,0.341200,0.087300,3.908,OK
100000,pthreads,4,2,0.338900,0.086100,3.936,OK
...
```

Averaged results contain one row per configuration:

```csv
degree,mode,threads,avg_serial,avg_parallel,avg_speedup
100000,pthreads,4,0.340050,0.086700,3.922
```

---

## Design Notes

### Shared shell library (`scripts/lib.sh`)

All bench and avg scripts source `lib.sh`, which provides:

| Function | Purpose |
|----------|---------|
| `setup_trap` | Kills child processes cleanly on Ctrl+C |
| `require_tools` | Aborts if `bc`, `awk`, or `sort` are missing |
| `require_program` | Aborts with a clear message if `bin/exN` is not built |
| `collect_system_info` | Writes machine specs to a text file |
| `avg_csv` | Generic awk-based averaging, configured per exercise via column indices |

### Compiler flags

```
-O2 -Wall -Wextra -Wpedantic -std=c11 -fopenmp -lpthread -lm
```

`-O2` is used rather than `-O3` to keep results reproducible and comparable with the department's reference environment.

### Input validation

All programs use `strtol` / `strtoll` for argument parsing rather than `atoi` / `atoll`. This catches: non-numeric input, trailing garbage, overflow, negative values, and zero. Every `malloc` and `calloc` call is checked via `xmalloc` / `xcalloc` helpers that print a descriptive error and call `exit(1)` on failure. All `pthread_create` and `pthread_join` calls check return codes and exit on error.
