# Parallel Systems — Programming Assignment 1

This project contains six C implementations for the first programming assignment of Parallel Computing Systems. 
The exercises cover Pthreads, OpenMP, synchronization, barriers, sparse matrix-vector multiplication, and task-based parallel mergesort.


## Repository layout

```text
.
├── Makefile
├── README.md
├── src/
│   ├── ex1.c
│   ├── ex2.c
│   ├── ex3.c
│   ├── ex4.c
│   ├── ex5.c
│   └── ex6.c
├── scripts/
│   ├── bench_ex1.sh ... bench_ex6.sh
│   ├── avg_ex1.sh ... avg_ex6.sh
│   ├── lib.sh
│   ├── sysinfo.sh
│   └── collect_sysinfo.sh
├── tests/
│   └── smoke-test-all.sh
└── results/
    └── ex1/ ... ex6/
```

`src/` contains the actual exercise implementations.  
`scripts/` contains benchmark and averaging scripts.  
`tests/` contains a quick smoke test for all exercises.  
`results/` is where benchmark outputs are written.

## Requirements

The code is intended for a Linux system with:

```text
GCC with OpenMP support
GNU Make
bash
awk
sort
Pthreads
libm
```

The Makefile compiles with C11, OpenMP, Pthreads, and math-library support.

## Build

Run all commands from the project root, the directory that contains the `Makefile`.

```bash
make
```

This creates the executables under `bin/`:

```text
bin/ex1
bin/ex2
bin/ex3
bin/ex4
bin/ex5
bin/ex6
```

To clean compiled binaries:

```bash
make clean
```

To rebuild from scratch:

```bash
make clean
make
```

If the Makefile does not define shortcut targets such as `make ex1`, compile all exercises with `make`, or build an individual binary with:

```bash
mkdir -p bin
make bin/ex1
```

Replace `ex1` with the required exercise number.

## Smoke test

Before running long benchmarks, run the smoke test:

```bash
bash tests/smoke-test-all.sh
```

Expected final line:

```text
[smoke] all tests passed
```

The smoke test rebuilds the project and runs small correctness checks for all supported modes.

## Exercise overview

### Exercise 1 — Polynomial multiplication

```bash
bin/ex1 <degree> <pthreads|openmp> <num_threads>
```

Example:

```bash
bin/ex1 10000 pthreads 4
bin/ex1 10000 openmp 4
```

This program multiplies two dense random polynomials. It runs a serial baseline and then a parallel implementation selected by the user. The result is checked against the serial result.

The important idea is that every output coefficient can be computed independently. Each thread owns different result positions, so no lock is required for the result array.

### Exercise 2 — Shared counter synchronization

```bash
bin/ex2 <num_threads> <iterations> <mutex|rwlock|atomic>
```

Example:

```bash
bin/ex2 4 1000000 mutex
bin/ex2 4 1000000 rwlock
bin/ex2 4 1000000 atomic
```

This program has all threads increment the same shared counter. It compares three synchronization methods: mutex, rwlock in write mode, and GCC atomic operations.

The expected result is deterministic:

```text
final counter = num_threads * iterations
```

The atomic version is expected to be fastest for this specific workload, because the operation is only a simple increment. 
The rwlock version is expected to be slower because every operation is a write, so the read-sharing advantage of rwlocks is not used.

### Exercise 3 — Bank simulation

```bash
bin/ex3 <num_accounts> <transactions_per_thread> <read_pct> \
        <coarse_mutex|fine_mutex|coarse_rw|fine_rw> \
        <num_threads> <read_work_iters>
```

Example:

```bash
bin/ex3 1000 10000 80 fine_rw 4 100
```

This program simulates a bank account array. Threads perform two kinds of transactions:

```text
money transfer: move money from one account to another
balance query: read one account balance
```

The four synchronization schemes are:

```text
coarse_mutex  -> one global mutex
fine_mutex    -> one mutex per account
coarse_rw     -> one global read-write lock
fine_rw       -> one read-write lock per account
```

Fine-grained transfers lock the two involved accounts in increasing account-index order. This prevents deadlock.

Correctness is checked by verifying that the total amount of money in all accounts is unchanged after all transactions.

`read_work_iters` increases the amount of work done inside read critical sections. This is useful for showing when rwlocks become beneficial: they help more when the workload is read-heavy and each read section lasts longer.

### Exercise 4 — Barrier implementations

```bash
bin/ex4 <num_threads> <iterations> <pthreads|condvar|sense>
```

Example:

```bash
bin/ex4 4 1000000 pthreads
bin/ex4 4 1000000 condvar
bin/ex4 4 1000000 sense
```

This program compares three reusable barriers:

```text
pthreads -> pthread_barrier_t
condvar  -> custom mutex + condition-variable barrier
sense    -> custom sense-reversal centralized barrier
```

The condition-variable barrier blocks waiting threads. The sense-reversal barrier uses atomic operations and spinning. The sense-reversal version can be fast when enough cores are available, but oversubscription can hurt because spinning threads consume CPU time.

### Exercise 5 — Sparse matrix-vector multiplication

```bash
bin/ex5 <matrix_size> <sparsity_pct> <iterations> <num_threads>
```

Example:

```bash
bin/ex5 2000 90 10 4
```

This program builds a sparse matrix in CSR format and compares sparse matrix-vector multiplication against dense matrix-vector multiplication.

CSR uses three arrays:

```text
values  -> non-zero values
col_idx -> column index for each non-zero value
row_ptr -> start/end offsets for each row
```

The program includes serial and parallel versions for CSR construction and SpMV. For repeated multiplication, the output vector of one iteration becomes the input vector of the next iteration.

When reporting performance, compare:

```text
parallel CSR total = CSR build parallel + CSR SpMV parallel
```

against dense SpMV. This matters because CSR has a construction cost.

### Exercise 6 — OpenMP task mergesort

```bash
bin/ex6 <array_size> <serial|parallel> <num_threads>
```

Example:

```bash
bin/ex6 10000000 serial 1
bin/ex6 10000000 parallel 4
```

This program implements top-down mergesort. The parallel version uses OpenMP tasks for recursive halves of the array.

A cutoff is used so that small subarrays are sorted serially instead of creating too many tiny tasks. This avoids task-creation overhead.

Correctness is checked by verifying that the final array is sorted.

## Benchmarks

Each exercise has a benchmark script:

```bash
bash scripts/bench_ex1.sh
bash scripts/bench_ex2.sh
bash scripts/bench_ex3.sh
bash scripts/bench_ex4.sh
bash scripts/bench_ex5.sh
bash scripts/bench_ex6.sh
```

Each benchmark writes raw CSV results and system information under `results/exN/`.

Example for exercise 3:

```text
results/ex3/bench_ex3_results.csv
results/ex3/bench_ex3_system.txt
```

The benchmark scripts also record the machine information used for that exercise, such as hostname, CPU model, core/thread count, OS, kernel, and compiler.

## Averaging benchmark results

After running a benchmark, run the corresponding averaging script:

```bash
bash scripts/avg_ex1.sh
bash scripts/avg_ex2.sh
bash scripts/avg_ex3.sh
bash scripts/avg_ex4.sh
bash scripts/avg_ex5.sh
bash scripts/avg_ex6.sh
```

This creates files like:

```text
results/ex1/bench_ex1_averages.csv
results/ex2/bench_ex2_averages.csv
...
```

Use the average CSV files for report tables and plots. Keep the raw CSV files as supporting data.

## Recommended workflow

For each exercise:

```bash
make clean
make
bash tests/smoke-test-all.sh
bash scripts/bench_exN.sh
bash scripts/avg_exN.sh
```

Replace `N` with the exercise number.


## Output and exit codes

Most programs print progress messages to `stderr` and final results to `stdout`.

Common exit codes:

```text
0 -> success
1 -> invalid arguments or runtime setup error
2 -> correctness check failed
```

Benchmark scripts parse the `stdout` timing lines and correctness lines.

## Practical notes

Some benchmark inputs are intentionally large and may take a long time. In particular:

```text
ex1 with very high polynomial degree is O(n^2)
ex5 with large dense matrices uses significant memory
ex6 with 100,000,000 integers uses significant memory and time
```

For ex1 bench scripts especially screen or tmux is necessary to keep the process alive in the background even after the ssh connection gets a timeout.

