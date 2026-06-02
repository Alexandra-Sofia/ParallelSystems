/**
 * @file ex5.c
 * @brief Exercise 1.5 — Sparse matrix-vector multiplication using CSR format.
 *
 * Constructs a random sparse matrix, converts it to Compressed Sparse Row
 * (CSR) format both serially and in parallel, then performs repeated sparse
 * matrix-vector multiplications (SpMV) in both modes. Results are compared
 * against the dense baseline for correctness. Timing is reported for all
 * four paths so that serial vs parallel speedup can be computed.
 *
 * For the repeated multiplication the output vector of each iteration
 * becomes the input vector of the next, modelling a real iterative solver.
 * All vectors use long long to reduce overflow risk during accumulation.
 *
 * Usage:
 *   ./ex5 <matrix_size> <sparsity_pct> <iterations> <num_threads>
 *
 * sparsity_pct: percentage of elements that are zero, e.g. 90 for 90% zeros.
 *
 * Example:
 *   ./ex5 2000 90 10 4
 */

#include <errno.h>
#include <limits.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SEED 42
#define MAX_SIZE 20000
#define MAX_THREADS 256

/**
 * @brief Return the current monotonic time in seconds.
 *
 * @return Wall-clock time as a double, in seconds.
 */
static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/**
 * @brief Compressed Sparse Row representation of a matrix.
 *
 * For an n x n matrix with nnz non-zero elements:
 *   values  : non-zero values in row-major order (size nnz)
 *   col_idx : column index of each non-zero (size nnz)
 *   row_ptr : row_ptr[i] is the index in values/col_idx where row i starts;
 *             row_ptr[n] == nnz (sentinel)
 */
typedef struct {
    int *values;  /**< Non-zero element values (size nnz). */
    int *col_idx; /**< Column indices of non-zeros (size nnz). */
    int *row_ptr; /**< Row start pointers (size n+1). */
    int n;        /**< Matrix dimension (square: n x n). */
    int nnz;      /**< Total number of non-zero elements. */
} csr_matrix_t;

/**
 * @brief Parse a string as a positive integer using strtol.
 *
 * @param str   Input string.
 * @param out   Output value on success.
 * @param max   Maximum accepted value (inclusive).
 * @param label Parameter name for error messages.
 * @return 1 on success, 0 on any error.
 */
static int parse_positive_int(const char *str, int *out, int max,
                              const char *label)
{
    if (str == NULL || str[0] == '\0') {
        fprintf(stderr, "Error: %s is empty\n", label);
        return 0;
    }

    char *end;
    errno = 0;
    long val = strtol(str, &end, 10);

    if (errno == ERANGE || val > INT_MAX || val < INT_MIN) {
        fprintf(stderr, "Error: %s '%s' overflows\n", label, str);
        return 0;
    }

    if (end == str || *end != '\0') {
        fprintf(stderr, "Error: %s '%s' is not a valid integer\n", label, str);
        return 0;
    }

    if (val <= 0) {
        fprintf(stderr, "Error: %s must be positive, got %ld\n", label, val);
        return 0;
    }

    if (val > max) {
        fprintf(stderr, "Error: %s %ld exceeds maximum of %d\n",
                label, val, max);
        return 0;
    }

    *out = (int)val;
    return 1;
}

/**
 * @brief Validate and parse all command-line arguments.
 *
 * @param argc        Argument count from main.
 * @param argv        Argument vector from main.
 * @param n           Output: matrix dimension.
 * @param sparsity    Output: percentage of zero elements [0,99].
 * @param iterations  Output: number of SpMV repetitions.
 * @param num_threads Output: OpenMP thread count.
 * @return 1 if all arguments are valid, 0 otherwise.
 */
static int parse_args(int argc, char *argv[],
                      int *n, int *sparsity, int *iterations, int *num_threads)
{
    if (argc != 5) {
        fprintf(stderr,
                "Usage: %s <matrix_size> <sparsity_pct> <iterations>"
                " <num_threads>\n", argv[0]);
        return 0;
    }

    if (!parse_positive_int(argv[1], n, MAX_SIZE, "matrix_size")) {
        return 0;
    }

    char *end;
    errno = 0;
    long sp = strtol(argv[2], &end, 10);
    if (errno == ERANGE || end == argv[2] || *end != '\0' ||
        sp < 0 || sp > 99) {
        fprintf(stderr,
                "Error: sparsity_pct must be an integer in [0,99], got '%s'\n",
                argv[2]);
        return 0;
    }
    *sparsity = (int)sp;

    if (!parse_positive_int(argv[3], iterations, 10000, "iterations")) {
        return 0;
    }

    if (!parse_positive_int(argv[4], num_threads, MAX_THREADS, "num_threads")) {
        return 0;
    }

    return 1;
}

/**
 * @brief Allocate memory and exit on failure.
 *
 * @param n    Number of elements.
 * @param size Size of each element in bytes.
 * @return Pointer to allocated memory, never NULL.
 */
static void *xmalloc(size_t n, size_t size)
{
    void *ptr = malloc(n * size);
    if (ptr == NULL) {
        fprintf(stderr, "Error: malloc failed for %zu bytes\n", n * size);
        exit(1);
    }
    return ptr;
}

/**
 * @brief Allocate zero-initialised memory and exit on failure.
 *
 * @param n    Number of elements.
 * @param size Size of each element in bytes.
 * @return Pointer to zeroed memory, never NULL.
 */
static void *xcalloc(size_t n, size_t size)
{
    void *ptr = calloc(n, size);
    if (ptr == NULL) {
        fprintf(stderr, "Error: calloc failed for %zu bytes\n", n * size);
        exit(1);
    }
    return ptr;
}

/**
 * @brief Allocate and fill a random sparse integer matrix in dense storage.
 *
 * Matrix values are kept small to reduce overflow risk during repeated SpMV.
 *
 * @param n            Matrix dimension.
 * @param sparsity_pct Percentage of zero elements [0, 99].
 * @return Heap-allocated n x n matrix in row-major order. Caller must free.
 */
static int *generate_matrix(int n, int sparsity_pct)
{
    int *mat = xmalloc((size_t)n * n, sizeof(int));
    srand(SEED);

    for (int i = 0; i < n * n; i++) {
        mat[i] = (rand() % 100) < sparsity_pct ? 0 : (rand() % 3) + 1;
    }

    return mat;
}

/**
 * @brief Allocate and fill a random long long vector of length n.
 *
 * Vector values are kept small to reduce overflow risk during repeated SpMV.
 *
 * @param n Vector length.
 * @return Heap-allocated array. Caller must free.
 */
static long long *generate_vector(int n)
{
    long long *vec = xmalloc((size_t)n, sizeof(long long));

    for (int i = 0; i < n; i++) {
        vec[i] = (rand() % 3) + 1;
    }

    return vec;
}

/**
 * @brief Build a CSR representation of a dense matrix serially.
 *
 * @param mat Dense matrix in row-major order (size n x n).
 * @param n   Matrix dimension.
 * @return Initialised CSR matrix. Call csr_free to release.
 */
static csr_matrix_t csr_build_serial(int *mat, int n)
{
    csr_matrix_t csr;
    csr.n = n;
    csr.row_ptr = xcalloc((size_t)(n + 1), sizeof(int));

    for (int i = 0; i < n; i++) {
        int count = 0;
        for (int j = 0; j < n; j++) {
            if (mat[i * n + j] != 0) {
                count++;
            }
        }
        csr.row_ptr[i + 1] = count;
    }

    for (int i = 1; i <= n; i++) {
        csr.row_ptr[i] += csr.row_ptr[i - 1];
    }
    csr.nnz = csr.row_ptr[n];

    csr.values = xmalloc((size_t)csr.nnz, sizeof(int));
    csr.col_idx = xmalloc((size_t)csr.nnz, sizeof(int));

    for (int i = 0; i < n; i++) {
        int pos = csr.row_ptr[i];
        for (int j = 0; j < n; j++) {
            int val = mat[i * n + j];
            if (val != 0) {
                csr.values[pos] = val;
                csr.col_idx[pos] = j;
                pos++;
            }
        }
    }

    return csr;
}

/**
 * @brief Build a CSR representation of a dense matrix in parallel.
 *
 * Phase 1: Count non-zeros per row in parallel.
 * Phase 2: Prefix sum over row_ptr, serial O(n).
 * Phase 3: Fill values and col_idx in parallel.
 *
 * @param mat         Dense matrix in row-major order (size n x n).
 * @param n           Matrix dimension.
 * @param num_threads Number of OpenMP threads to use.
 * @return Initialised CSR matrix. Call csr_free to release.
 */
static csr_matrix_t csr_build_parallel(int *mat, int n, int num_threads)
{
    csr_matrix_t csr;
    csr.n = n;
    csr.row_ptr = xcalloc((size_t)(n + 1), sizeof(int));

    omp_set_num_threads(num_threads);

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; i++) {
        int count = 0;
        for (int j = 0; j < n; j++) {
            if (mat[i * n + j] != 0) {
                count++;
            }
        }
        csr.row_ptr[i + 1] = count;
    }

    for (int i = 1; i <= n; i++) {
        csr.row_ptr[i] += csr.row_ptr[i - 1];
    }
    csr.nnz = csr.row_ptr[n];

    csr.values = xmalloc((size_t)csr.nnz, sizeof(int));
    csr.col_idx = xmalloc((size_t)csr.nnz, sizeof(int));

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; i++) {
        int pos = csr.row_ptr[i];
        for (int j = 0; j < n; j++) {
            int val = mat[i * n + j];
            if (val != 0) {
                csr.values[pos] = val;
                csr.col_idx[pos] = j;
                pos++;
            }
        }
    }

    return csr;
}

/**
 * @brief Release all memory owned by a CSR matrix.
 *
 * @param csr Pointer to the CSR matrix to free.
 */
static void csr_free(csr_matrix_t *csr)
{
    free(csr->values);
    free(csr->col_idx);
    free(csr->row_ptr);
}

/**
 * @brief Serial CSR SpMV: y = A * x.
 *
 * @param csr CSR matrix.
 * @param x   Input vector (size n).
 * @param y   Output vector (size n); must be pre-allocated.
 */
static void spmv_csr_serial(const csr_matrix_t *csr, long long *x,
                            long long *y)
{
    for (int i = 0; i < csr->n; i++) {
        long long sum = 0;
        for (int k = csr->row_ptr[i]; k < csr->row_ptr[i + 1]; k++) {
            sum += (long long)csr->values[k] * x[csr->col_idx[k]];
        }
        y[i] = sum;
    }
}

/**
 * @brief Parallel CSR SpMV: y = A * x.
 *
 * Each row is an independent dot product; no synchronisation required.
 *
 * @param csr         CSR matrix.
 * @param x           Input vector (size n).
 * @param y           Output vector (size n); must be pre-allocated.
 * @param num_threads Number of OpenMP threads.
 */
static void spmv_csr_parallel(const csr_matrix_t *csr, long long *x,
                              long long *y, int num_threads)
{
    omp_set_num_threads(num_threads);

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < csr->n; i++) {
        long long sum = 0;
        for (int k = csr->row_ptr[i]; k < csr->row_ptr[i + 1]; k++) {
            sum += (long long)csr->values[k] * x[csr->col_idx[k]];
        }
        y[i] = sum;
    }
}

/**
 * @brief Serial dense SpMV: y = A * x.
 *
 * @param mat Dense matrix in row-major order (size n x n).
 * @param x   Input vector (size n).
 * @param y   Output vector (size n); must be pre-allocated.
 * @param n   Matrix dimension.
 */
static void spmv_dense_serial(int *mat, long long *x, long long *y, int n)
{
    for (int i = 0; i < n; i++) {
        long long sum = 0;
        for (int j = 0; j < n; j++) {
            sum += (long long)mat[i * n + j] * x[j];
        }
        y[i] = sum;
    }
}

/**
 * @brief Parallel dense SpMV: y = A * x.
 *
 * @param mat         Dense matrix in row-major order (size n x n).
 * @param x           Input vector (size n).
 * @param y           Output vector (size n); must be pre-allocated.
 * @param n           Matrix dimension.
 * @param num_threads Number of OpenMP threads.
 */
static void spmv_dense_parallel(int *mat, long long *x, long long *y, int n,
                                int num_threads)
{
    omp_set_num_threads(num_threads);

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; i++) {
        long long sum = 0;
        for (int j = 0; j < n; j++) {
            sum += (long long)mat[i * n + j] * x[j];
        }
        y[i] = sum;
    }
}

/**
 * @brief Compare two long long vectors of length n for equality.
 *
 * @param a First vector.
 * @param b Second vector.
 * @param n Length.
 * @return 1 if all elements are equal, 0 otherwise.
 */
static int vectors_match(long long *a, long long *b, int n)
{
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Program entry point.
 *
 * @param argc Argument count.
 * @param argv matrix_size, sparsity_pct, iterations, num_threads.
 * @return 0 on success, 1 on argument error, 2 on correctness failure.
 */
int main(int argc, char *argv[])
{
    int n;
    int sparsity_pct;
    int iterations;
    int num_threads;

    if (!parse_args(argc, argv, &n, &sparsity_pct, &iterations, &num_threads)) {
        return 1;
    }

    fprintf(stderr,
            "[ex5] size=%dx%d sparsity=%d%% iterations=%d threads=%d\n",
            n, n, sparsity_pct, iterations, num_threads);
    fprintf(stderr, "[ex5] generating matrix and vector...\n");

    int *mat = generate_matrix(n, sparsity_pct);
    long long *x0 = generate_vector(n);

    long long *buf_a = xmalloc((size_t)n, sizeof(long long));
    long long *buf_b = xmalloc((size_t)n, sizeof(long long));

    long long *result_csr_s = xmalloc((size_t)n, sizeof(long long));
    long long *result_csr_p = xmalloc((size_t)n, sizeof(long long));
    long long *result_dense_s = xmalloc((size_t)n, sizeof(long long));
    long long *result_dense_p = xmalloc((size_t)n, sizeof(long long));

    fprintf(stderr, "[ex5] building CSR (serial)...\n");
    double t0 = now();
    csr_matrix_t csr_s = csr_build_serial(mat, n);
    double t_csr_serial = now() - t0;

    fprintf(stderr, "[ex5] building CSR (parallel, %d threads)...\n",
            num_threads);
    double t1 = now();
    csr_matrix_t csr_p = csr_build_parallel(mat, n, num_threads);
    double t_csr_parallel = now() - t1;

    fprintf(stderr, "[ex5] CSR builds done. NNZ=%d (%.1f%%)\n",
            csr_p.nnz, 100.0 * csr_p.nnz / ((long long)n * n));

    double t_spmv_csr_serial;
    double t_spmv_csr_parallel;
    double t_spmv_dense_serial;
    double t_spmv_dense_parallel;

    fprintf(stderr, "[ex5] running serial CSR SpMV (%d iterations)...\n",
            iterations);
    {
        long long *x = buf_a;
        long long *y = buf_b;
        memcpy(x, x0, (size_t)n * sizeof(long long));
        double t2 = now();
        for (int it = 0; it < iterations; it++) {
            spmv_csr_serial(&csr_s, x, y);
            long long *tmp = x;
            x = y;
            y = tmp;
        }
        t_spmv_csr_serial = now() - t2;
        memcpy(result_csr_s, x, (size_t)n * sizeof(long long));
    }

    fprintf(stderr, "[ex5] running parallel CSR SpMV (%d iterations)...\n",
            iterations);
    {
        long long *x = buf_a;
        long long *y = buf_b;
        memcpy(x, x0, (size_t)n * sizeof(long long));
        double t3 = now();
        for (int it = 0; it < iterations; it++) {
            spmv_csr_parallel(&csr_p, x, y, num_threads);
            long long *tmp = x;
            x = y;
            y = tmp;
        }
        t_spmv_csr_parallel = now() - t3;
        memcpy(result_csr_p, x, (size_t)n * sizeof(long long));
    }

    fprintf(stderr, "[ex5] running serial dense SpMV (%d iterations)...\n",
            iterations);
    {
        long long *x = buf_a;
        long long *y = buf_b;
        memcpy(x, x0, (size_t)n * sizeof(long long));
        double t4 = now();
        for (int it = 0; it < iterations; it++) {
            spmv_dense_serial(mat, x, y, n);
            long long *tmp = x;
            x = y;
            y = tmp;
        }
        t_spmv_dense_serial = now() - t4;
        memcpy(result_dense_s, x, (size_t)n * sizeof(long long));
    }

    fprintf(stderr, "[ex5] running parallel dense SpMV (%d iterations)...\n",
            iterations);
    {
        long long *x = buf_a;
        long long *y = buf_b;
        memcpy(x, x0, (size_t)n * sizeof(long long));
        double t5 = now();
        for (int it = 0; it < iterations; it++) {
            spmv_dense_parallel(mat, x, y, n, num_threads);
            long long *tmp = x;
            x = y;
            y = tmp;
        }
        t_spmv_dense_parallel = now() - t5;
        memcpy(result_dense_p, x, (size_t)n * sizeof(long long));
    }

    printf("Matrix size:        %d x %d\n", n, n);
    printf("Sparsity:           %d%%\n", sparsity_pct);
    printf("NNZ:                %d (%.1f%%)\n", csr_p.nnz,
           100.0 * csr_p.nnz / ((long long)n * n));
    printf("Threads:            %d\n", num_threads);
    printf("Iterations:         %d\n", iterations);
    printf("\n");
    printf("CSR build serial:   %.6f s\n", t_csr_serial);
    printf("CSR build parallel: %.6f s  (speedup %.2fx)\n",
           t_csr_parallel,
           t_csr_serial / t_csr_parallel);
    printf("\n");
    printf("CSR SpMV serial:    %.6f s\n", t_spmv_csr_serial);
    printf("CSR SpMV parallel:  %.6f s  (speedup %.2fx)\n",
           t_spmv_csr_parallel,
           t_spmv_csr_serial / t_spmv_csr_parallel);
    printf("\n");
    printf("Dense SpMV serial:  %.6f s\n", t_spmv_dense_serial);
    printf("Dense SpMV parallel: %.6f s  (speedup %.2fx)\n",
           t_spmv_dense_parallel,
           t_spmv_dense_serial / t_spmv_dense_parallel);
    printf("\n");
    printf("CSR vs Dense serial:   %.2fx\n",
           t_spmv_dense_serial / t_spmv_csr_serial);
    printf("CSR vs Dense parallel: %.2fx\n",
           t_spmv_dense_parallel / t_spmv_csr_parallel);

    int ok = vectors_match(result_csr_s, result_csr_p, n) &&
             vectors_match(result_csr_s, result_dense_s, n) &&
             vectors_match(result_csr_s, result_dense_p, n);

    printf("\nCorrectness:        %s\n", ok ? "[OK]" : "[FAIL]");
    fprintf(stderr, "[ex5] done\n");

    csr_free(&csr_s);
    csr_free(&csr_p);
    free(mat);
    free(x0);
    free(buf_a);
    free(buf_b);
    free(result_csr_s);
    free(result_csr_p);
    free(result_dense_s);
    free(result_dense_p);

    return ok ? 0 : 2;
}
