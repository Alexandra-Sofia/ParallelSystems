/**
 * @file ex5.c
 * @brief Exercise 1.5 — Sparse matrix-vector multiplication using CSR format.
 *
 * Constructs a random sparse matrix, converts it to Compressed Sparse Row
 * (CSR) format in parallel, then performs repeated sparse matrix-vector
 * multiplications (SpMV). Results are compared against the dense baseline
 * for correctness. Both CSR construction and SpMV are parallelised with
 * OpenMP.
 *
 * Usage:
 *   ./ex5 <matrix_size> <sparsity_pct> <iterations> <num_threads>
 *
 * sparsity_pct: percentage of elements that are zero, e.g. 90 for 90% zeros.
 *
 * Example:
 *   ./ex5 2000 90 10 4
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <omp.h>

#define SEED 42

/* -------------------------------------------------------------------------
 * Timing helpers
 * ---------------------------------------------------------------------- */

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

/* -------------------------------------------------------------------------
 * CSR data structure
 * ---------------------------------------------------------------------- */

/**
 * @brief Compressed Sparse Row representation of a matrix.
 *
 * For an n x n matrix with nnz non-zero elements:
 *   values   : the non-zero values in row-major order (size nnz)
 *   col_idx  : the column index of each non-zero (size nnz)
 *   row_ptr  : row_ptr[i] is the index in values/col_idx where row i starts;
 *              row_ptr[n] == nnz (sentinel)
 */
typedef struct {
    int *values;   /**< Non-zero element values (size nnz). */
    int *col_idx;  /**< Column indices of non-zeros (size nnz). */
    int *row_ptr;  /**< Row start pointers (size n+1). */
    int n;         /**< Matrix dimension (square: n x n). */
    int nnz;       /**< Total number of non-zero elements. */
} csr_matrix_t;

/* -------------------------------------------------------------------------
 * Matrix generation
 * ---------------------------------------------------------------------- */

/**
 * @brief Allocate and fill a random sparse integer matrix (dense storage).
 *
 * Each element is independently set to zero with probability sparsity_pct/100
 * or to a random value in [1, 99] otherwise.
 *
 * @param n           Matrix dimension.
 * @param sparsity_pct Percentage of zero elements [0, 100].
 * @return Heap-allocated n x n matrix in row-major order. Caller must free.
 */
static int *generate_sparse_matrix(int n, int sparsity_pct)
{
    int *mat = malloc((size_t)n * n * sizeof(int));
    srand(SEED);
    for (int i = 0; i < n * n; i++) {
        if ((rand() % 100) < sparsity_pct) {
            mat[i] = 0;
        } else {
            mat[i] = (rand() % 99) + 1;
        }
    }
    return mat;
}

/**
 * @brief Allocate and fill a random integer vector of length n.
 *
 * @param n Vector length.
 * @return Heap-allocated array. Caller must free.
 */
static int *generate_vector(int n)
{
    int *vec = malloc((size_t)n * sizeof(int));
    for (int i = 0; i < n; i++) {
        vec[i] = (rand() % 99) + 1;
    }
    return vec;
}

/* -------------------------------------------------------------------------
 * CSR construction
 * ---------------------------------------------------------------------- */

/**
 * @brief Build a CSR representation of a dense matrix in parallel.
 *
 * Phase 1: Count non-zeros per row in parallel.
 * Phase 2: Compute the prefix sum (row_ptr) — done serially as it is O(n).
 * Phase 3: Fill values and col_idx arrays in parallel using a per-row cursor.
 *
 * @param mat          Dense matrix in row-major order (size n x n).
 * @param n            Matrix dimension.
 * @param num_threads  Number of OpenMP threads to use.
 * @return Initialised CSR matrix. Call csr_free to release.
 */
static csr_matrix_t csr_build(int *mat, int n, int num_threads)
{
    csr_matrix_t csr;
    csr.n = n;
    csr.row_ptr = calloc((size_t)(n + 1), sizeof(int));

    omp_set_num_threads(num_threads);

    /* Phase 1: count non-zeros per row in parallel. */
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

    /* Phase 2: exclusive prefix sum to build row_ptr. */
    csr.row_ptr[0] = 0;
    for (int i = 1; i <= n; i++) {
        csr.row_ptr[i] += csr.row_ptr[i - 1];
    }
    csr.nnz = csr.row_ptr[n];

    csr.values = malloc((size_t)csr.nnz * sizeof(int));
    csr.col_idx = malloc((size_t)csr.nnz * sizeof(int));

    /* Phase 3: fill values and col_idx in parallel. */
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

/* -------------------------------------------------------------------------
 * SpMV kernels
 * ---------------------------------------------------------------------- */

/**
 * @brief Sparse matrix-vector multiplication using CSR format (parallel).
 *
 * Each row of the matrix maps to one dot product. Rows are independent,
 * so no synchronisation is required.
 *
 * @param csr         CSR matrix.
 * @param x           Input vector (size n).
 * @param y           Output vector (size n); must be pre-allocated.
 * @param num_threads Number of OpenMP threads to use.
 */
static void spmv_csr(const csr_matrix_t *csr, int *x, long long *y,
                     int num_threads)
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
 * @brief Dense matrix-vector multiplication (parallel baseline).
 *
 * Used for performance comparison against the CSR kernel. No zeros are
 * skipped — all n^2 multiplications are performed.
 *
 * @param mat         Dense matrix in row-major order (size n x n).
 * @param x           Input vector (size n).
 * @param y           Output vector (size n); must be pre-allocated.
 * @param n           Matrix dimension.
 * @param num_threads Number of OpenMP threads to use.
 */
static void spmv_dense(int *mat, int *x, long long *y, int n, int num_threads)
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

/* -------------------------------------------------------------------------
 * Verification
 * ---------------------------------------------------------------------- */

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

/* -------------------------------------------------------------------------
 * Entry point
 * ---------------------------------------------------------------------- */

/**
 * @brief Program entry point.
 *
 * @param argc Argument count.
 * @param argv matrix_size, sparsity_pct, iterations, num_threads.
 * @return 0 on success, 1 on error.
 */
int main(int argc, char *argv[])
{
    if (argc != 5) {
        fprintf(stderr,
                "Usage: %s <matrix_size> <sparsity_pct> <iterations> "
                "<num_threads>\n",
                argv[0]);
        return 1;
    }

    int n = atoi(argv[1]);
    int sparsity_pct = atoi(argv[2]);
    int iterations = atoi(argv[3]);
    int num_threads = atoi(argv[4]);

    /* Matrix and vector generation (not timed per the assignment) */
    int *mat = generate_sparse_matrix(n, sparsity_pct);
    int *x = generate_vector(n);
    long long *y_csr = calloc((size_t)n, sizeof(long long));
    long long *y_dense = calloc((size_t)n, sizeof(long long));

    /* CSR construction */
    double t0 = now();
    csr_matrix_t csr = csr_build(mat, n, num_threads);
    double t_csr_build = now() - t0;

    printf("Matrix size:   %d x %d\n", n, n);
    printf("Sparsity:      %d%%\n", sparsity_pct);
    printf("NNZ:           %d (%.1f%%)\n", csr.nnz,
           100.0 * csr.nnz / ((long long)n * n));
    printf("Threads:       %d\n", num_threads);
    printf("CSR build:     %.6f s\n", t_csr_build);

    /* CSR SpMV — first pass used for correctness check */
    spmv_csr(&csr, x, y_csr, num_threads);

    /* CSR SpMV timed over 'iterations' passes */
    double t1 = now();
    for (int it = 0; it < iterations; it++) {
        spmv_csr(&csr, x, y_csr, num_threads);
    }
    double t_csr_spmv = now() - t1;

    /* Dense SpMV timed over 'iterations' passes */
    double t2 = now();
    for (int it = 0; it < iterations; it++) {
        spmv_dense(mat, x, y_dense, n, num_threads);
    }
    double t_dense_spmv = now() - t2;

    printf("CSR SpMV:      %.6f s  (%d iterations)\n", t_csr_spmv, iterations);
    printf("Dense SpMV:    %.6f s  (%d iterations)\n", t_dense_spmv, iterations);
    printf("CSR speedup:   %.2fx vs dense\n", t_dense_spmv / t_csr_spmv);

    /* Correctness: CSR and dense must produce the same output vector */
    spmv_csr(&csr, x, y_csr, num_threads);
    spmv_dense(mat, x, y_dense, n, num_threads);
    printf("Correctness:   %s\n",
           vectors_match(y_csr, y_dense, n) ? "[OK]" : "[FAIL]");

    csr_free(&csr);
    free(mat);
    free(x);
    free(y_csr);
    free(y_dense);
    return 0;
}
