/**
 * @file ex1.c
 * @brief Exercise 1.1 — Polynomial multiplication with Pthreads and OpenMP.
 *
 * Generates two random dense polynomials of degree n, multiplies them using
 * a serial O(n^2) algorithm, then repeats using either Pthreads or OpenMP
 * (selected via CLI). Verifies that both results match.
 *
 * Usage:
 *   ./ex1 <degree> <pthreads|openmp> <num_threads>
 *
 * Example:
 *   ./ex1 100000 pthreads 4
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
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
 * Data types
 * ---------------------------------------------------------------------- */

/**
 * @brief Arguments passed to each Pthreads worker thread.
 */
typedef struct {
    int *a;           /**< Coefficients of the first polynomial. */
    int *b;           /**< Coefficients of the second polynomial. */
    long long *result; /**< Output array (size 2*degree+1). */
    int degree;       /**< Degree of each input polynomial. */
    int thread_id;    /**< Zero-based index of this thread. */
    int num_threads;  /**< Total number of worker threads. */
} poly_args_t;

/* -------------------------------------------------------------------------
 * Serial implementation
 * ---------------------------------------------------------------------- */

/**
 * @brief Multiply two polynomials using the serial O(n^2) algorithm.
 *
 * Computes result[k] = sum_{i=0}^{degree} a[i] * b[k-i] for each k.
 * The caller must allocate result with at least 2*degree+1 elements,
 * pre-initialised to zero.
 *
 * @param a      Coefficients of the first polynomial (size degree+1).
 * @param b      Coefficients of the second polynomial (size degree+1).
 * @param result Output coefficient array (size 2*degree+1).
 * @param degree Degree of each input polynomial.
 */
void poly_multiply_serial(int *a, int *b, long long *result, int degree)
{
    int size = degree + 1;
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            result[i + j] += (long long)a[i] * b[j];
        }
    }
}

/* -------------------------------------------------------------------------
 * Pthreads implementation
 * ---------------------------------------------------------------------- */

/**
 * @brief Worker function executed by each Pthreads thread.
 *
 * Each thread owns a contiguous slice of the output coefficient array.
 * Because each output index k = i+j is written by exactly one thread,
 * no synchronisation is required.
 *
 * @param arg Pointer to a poly_args_t struct for this thread.
 * @return NULL always.
 */
static void *pthread_worker(void *arg)
{
    poly_args_t *args = (poly_args_t *)arg;
    int size = args->degree + 1;
    int result_size = 2 * args->degree + 1;

    int chunk = (result_size + args->num_threads - 1) / args->num_threads;
    int start = args->thread_id * chunk;
    int end = start + chunk;
    if (end > result_size) {
        end = result_size;
    }

    for (int k = start; k < end; k++) {
        long long sum = 0;
        int i_start = k - args->degree;
        if (i_start < 0) {
            i_start = 0;
        }
        int i_end = k;
        if (i_end >= size) {
            i_end = size - 1;
        }
        for (int i = i_start; i <= i_end; i++) {
            sum += (long long)args->a[i] * args->b[k - i];
        }
        args->result[k] = sum;
    }
    return NULL;
}

/**
 * @brief Multiply two polynomials in parallel using Pthreads.
 *
 * Partitions the output coefficient array across threads. Each thread
 * computes its slice independently with no shared writes.
 *
 * @param a           Coefficients of the first polynomial (size degree+1).
 * @param b           Coefficients of the second polynomial (size degree+1).
 * @param result      Output array (size 2*degree+1), pre-zeroed.
 * @param degree      Degree of each input polynomial.
 * @param num_threads Number of Pthreads worker threads to spawn.
 */
void poly_multiply_pthreads(int *a, int *b, long long *result, int degree,
                            int num_threads)
{
    pthread_t *threads = malloc((size_t)num_threads * sizeof(pthread_t));
    poly_args_t *args = malloc((size_t)num_threads * sizeof(poly_args_t));

    for (int t = 0; t < num_threads; t++) {
        args[t].a = a;
        args[t].b = b;
        args[t].result = result;
        args[t].degree = degree;
        args[t].thread_id = t;
        args[t].num_threads = num_threads;
        pthread_create(&threads[t], NULL, pthread_worker, &args[t]);
    }
    for (int t = 0; t < num_threads; t++) {
        pthread_join(threads[t], NULL);
    }

    free(threads);
    free(args);
}

/* -------------------------------------------------------------------------
 * OpenMP implementation
 * ---------------------------------------------------------------------- */

/**
 * @brief Multiply two polynomials in parallel using OpenMP.
 *
 * Distributes iterations of the output loop across threads using a static
 * schedule. Each iteration writes to a distinct index, so no reduction or
 * critical section is needed.
 *
 * @param a           Coefficients of the first polynomial (size degree+1).
 * @param b           Coefficients of the second polynomial (size degree+1).
 * @param result      Output array (size 2*degree+1), pre-zeroed.
 * @param degree      Degree of each input polynomial.
 * @param num_threads Number of OpenMP threads to use.
 */
void poly_multiply_openmp(int *a, int *b, long long *result, int degree,
                          int num_threads)
{
    int size = degree + 1;
    int result_size = 2 * degree + 1;

    omp_set_num_threads(num_threads);

    #pragma omp parallel for schedule(static)
    for (int k = 0; k < result_size; k++) {
        long long sum = 0;
        int i_start = k - degree;
        if (i_start < 0) {
            i_start = 0;
        }
        int i_end = k;
        if (i_end >= size) {
            i_end = size - 1;
        }
        for (int i = i_start; i <= i_end; i++) {
            sum += (long long)a[i] * b[k - i];
        }
        result[k] = sum;
    }
}

/* -------------------------------------------------------------------------
 * Verification
 * ---------------------------------------------------------------------- */

/**
 * @brief Compare two result arrays element-by-element.
 *
 * @param a    First array.
 * @param b    Second array.
 * @param size Number of elements.
 * @return 1 if all elements are equal, 0 otherwise.
 */
static int results_match(long long *a, long long *b, int size)
{
    for (int i = 0; i < size; i++) {
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
 * Parses arguments, allocates polynomials, runs serial and parallel
 * multiplications, prints timing, and verifies correctness.
 *
 * @param argc Argument count.
 * @param argv Argument vector: degree, mode (pthreads|openmp), num_threads.
 * @return 0 on success, 1 on argument error.
 */
int main(int argc, char *argv[])
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <degree> <pthreads|openmp> <num_threads>\n",
                argv[0]);
        return 1;
    }

    int degree = atoi(argv[1]);
    const char *mode = argv[2];
    int num_threads = atoi(argv[3]);
    int size = degree + 1;
    int result_size = 2 * degree + 1;

    /* Allocation and initialisation */
    double t_start = now();

    int *a = malloc((size_t)size * sizeof(int));
    int *b = malloc((size_t)size * sizeof(int));
    long long *result_serial = calloc((size_t)result_size, sizeof(long long));
    long long *result_parallel = calloc((size_t)result_size, sizeof(long long));

    srand(SEED);
    for (int i = 0; i < size; i++) {
        a[i] = (rand() % 100) + 1;
        b[i] = (rand() % 100) + 1;
    }

    double t_init = now() - t_start;
    printf("Init time:     %.6f s\n", t_init);

    /* Serial multiplication */
    double t0 = now();
    poly_multiply_serial(a, b, result_serial, degree);
    double t_serial = now() - t0;
    printf("Serial time:   %.6f s\n", t_serial);

    /* Parallel multiplication */
    double t1 = now();
    if (strcmp(mode, "pthreads") == 0) {
        poly_multiply_pthreads(a, b, result_parallel, degree, num_threads);
    } else if (strcmp(mode, "openmp") == 0) {
        poly_multiply_openmp(a, b, result_parallel, degree, num_threads);
    } else {
        fprintf(stderr, "Unknown mode: %s (use pthreads or openmp)\n", mode);
        return 1;
    }
    double t_parallel = now() - t1;
    printf("Parallel time: %.6f s  [%s, %d threads]\n",
           t_parallel, mode, num_threads);
    printf("Speedup:       %.2fx\n", t_serial / t_parallel);

    /* Verification */
    if (results_match(result_serial, result_parallel, result_size)) {
        printf("Correctness:   [OK]\n");
    } else {
        printf("Correctness:   [FAIL]\n");
    }

    free(a);
    free(b);
    free(result_serial);
    free(result_parallel);
    return 0;
}
