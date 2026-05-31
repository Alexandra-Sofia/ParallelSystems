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
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <pthread.h>
#include <omp.h>

#define SEED        42
#define MAX_THREADS 256
#define MAX_DEGREE  2000000

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
    int *a;            /**< Coefficients of the first polynomial. */
    int *b;            /**< Coefficients of the second polynomial. */
    long long *result; /**< Output array (size 2*degree+1). */
    int degree;        /**< Degree of each input polynomial. */
    int thread_id;     /**< Zero-based index of this thread. */
    int num_threads;   /**< Total number of worker threads. */
} poly_args_t;

/* -------------------------------------------------------------------------
 * Input parsing
 * ---------------------------------------------------------------------- */

/**
 * @brief Parse a string as a positive integer using strtol.
 *
 * Rejects empty strings, non-numeric input, values with trailing garbage,
 * negative values, zero, and values exceeding max.
 *
 * @param str   Input string to parse.
 * @param out   Output: parsed integer value on success.
 * @param max   Maximum accepted value (inclusive).
 * @param label Name of the parameter, used in error messages.
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
 * @param degree      Output: parsed polynomial degree.
 * @param mode        Output: pointer into argv for the mode string.
 * @param num_threads Output: parsed thread count.
 * @return 1 if all arguments are valid, 0 otherwise.
 */
static int parse_args(int argc, char *argv[], int *degree, const char **mode,
                      int *num_threads)
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <degree> <pthreads|openmp> <num_threads>\n",
                argv[0]);
        return 0;
    }

    if (!parse_positive_int(argv[1], degree, MAX_DEGREE, "degree")) {
        return 0;
    }

    *mode = argv[2];
    if (strcmp(*mode, "pthreads") != 0 && strcmp(*mode, "openmp") != 0) {
        fprintf(stderr,
                "Error: mode must be 'pthreads' or 'openmp', got '%s'\n",
                *mode);
        return 0;
    }

    if (!parse_positive_int(argv[3], num_threads, MAX_THREADS, "num_threads")) {
        return 0;
    }

    return 1;
}

/* -------------------------------------------------------------------------
 * Allocation helpers
 * ---------------------------------------------------------------------- */

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
    if (!ptr) {
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
    if (!ptr) {
        fprintf(stderr, "Error: calloc failed for %zu bytes\n", n * size);
        exit(1);
    }
    return ptr;
}

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
static void poly_multiply_serial(int *a, int *b, long long *result, int degree)
{
    int size = degree + 1;
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            result[i + j] += (long long)a[i] * b[j];
        }
    }
}

/* -------------------------------------------------------------------------
 * Pthreads implementation — cyclic distribution
 * ---------------------------------------------------------------------- */

/**
 * @brief Worker function executed by each Pthreads thread.
 *
 * Uses cyclic (interleaved) distribution over output coefficients rather
 * than block distribution. Thread t computes k = t, t+T, t+2T, ... where
 * T is the total thread count. This balances load naturally: the work per
 * coefficient k peaks at the middle of the result array (degree+1 terms)
 * and tapers toward the edges. Cyclic distribution gives every thread a
 * mix of cheap and expensive coefficients, avoiding the imbalance that
 * block distribution creates when the middle threads receive all the heavy
 * coefficients.
 *
 * No synchronisation is required because each k is owned by exactly one
 * thread.
 *
 * @param arg Pointer to a poly_args_t struct for this thread.
 * @return NULL always.
 */
static void *pthread_worker(void *arg)
{
    poly_args_t *args = (poly_args_t *)arg;
    int size = args->degree + 1;
    int result_size = 2 * args->degree + 1;

    for (int k = args->thread_id; k < result_size; k += args->num_threads) {
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
 * Spawns num_threads threads with cyclic distribution over output
 * coefficients. Each thread computes its assigned indices independently.
 *
 * @param a           Coefficients of the first polynomial (size degree+1).
 * @param b           Coefficients of the second polynomial (size degree+1).
 * @param result      Output array (size 2*degree+1), pre-zeroed.
 * @param degree      Degree of each input polynomial.
 * @param num_threads Number of Pthreads worker threads to spawn.
 */
static void poly_multiply_pthreads(int *a, int *b, long long *result,
                                   int degree, int num_threads)
{
    pthread_t *threads = xmalloc((size_t)num_threads, sizeof(pthread_t));
    poly_args_t *args = xmalloc((size_t)num_threads, sizeof(poly_args_t));

    for (int t = 0; t < num_threads; t++) {
        args[t].a = a;
        args[t].b = b;
        args[t].result = result;
        args[t].degree = degree;
        args[t].thread_id = t;
        args[t].num_threads = num_threads;

        int rc = pthread_create(&threads[t], NULL, pthread_worker, &args[t]);
        if (rc != 0) {
            fprintf(stderr, "Error: pthread_create failed for thread %d: %s\n",
                    t, strerror(rc));
            exit(1);
        }
    }
    for (int t = 0; t < num_threads; t++) {
        int rc = pthread_join(threads[t], NULL);
        if (rc != 0) {
            fprintf(stderr, "Error: pthread_join failed for thread %d: %s\n",
                    t, strerror(rc));
            exit(1);
        }
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
 * Uses schedule(static) which applies the same cyclic-like distribution
 * that the Pthreads implementation does manually. Each iteration writes
 * to a distinct index so no reduction or critical section is needed.
 *
 * @param a           Coefficients of the first polynomial (size degree+1).
 * @param b           Coefficients of the second polynomial (size degree+1).
 * @param result      Output array (size 2*degree+1), pre-zeroed.
 * @param degree      Degree of each input polynomial.
 * @param num_threads Number of OpenMP threads to use.
 */
static void poly_multiply_openmp(int *a, int *b, long long *result, int degree,
                                 int num_threads)
{
    int size = degree + 1;
    int result_size = 2 * degree + 1;

    omp_set_num_threads(num_threads);

    #pragma omp parallel for schedule(static, 1)
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
 * @return 0 on success, 1 on argument error, 2 on correctness failure.
 */
int main(int argc, char *argv[])
{
    int degree, num_threads;
    const char *mode;

    if (!parse_args(argc, argv, &degree, &mode, &num_threads)) {
        return 1;
    }

    int size = degree + 1;
    int result_size = 2 * degree + 1;

    fprintf(stderr, "[ex1] degree=%d mode=%s threads=%d\n",
            degree, mode, num_threads);
    fprintf(stderr, "[ex1] allocating and generating polynomials...\n");

    double t_start = now();

    int *a = xmalloc((size_t)size, sizeof(int));
    int *b = xmalloc((size_t)size, sizeof(int));
    long long *result_serial   = xcalloc((size_t)result_size, sizeof(long long));
    long long *result_parallel = xcalloc((size_t)result_size, sizeof(long long));

    srand(SEED);
    for (int i = 0; i < size; i++) {
        a[i] = (rand() % 100) + 1;
        b[i] = (rand() % 100) + 1;
    }

    double t_init = now() - t_start;
    printf("Init time:     %.6f s\n", t_init);
    fprintf(stderr, "[ex1] init done (%.6f s)\n", t_init);

    /* Serial multiplication */
    fprintf(stderr, "[ex1] running serial multiply...\n");
    double t0 = now();
    poly_multiply_serial(a, b, result_serial, degree);
    double t_serial = now() - t0;
    printf("Serial time:   %.6f s\n", t_serial);
    fprintf(stderr, "[ex1] serial done (%.6f s)\n", t_serial);

    /* Parallel multiplication */
    fprintf(stderr, "[ex1] running parallel multiply...\n");
    double t1 = now();
    if (strcmp(mode, "pthreads") == 0) {
        poly_multiply_pthreads(a, b, result_parallel, degree, num_threads);
    } else {
        poly_multiply_openmp(a, b, result_parallel, degree, num_threads);
    }
    double t_parallel = now() - t1;
    printf("Parallel time: %.6f s  [%s, %d threads]\n",
           t_parallel, mode, num_threads);
    printf("Speedup:       %.2fx\n", t_serial / t_parallel);
    fprintf(stderr, "[ex1] parallel done (%.6f s)\n", t_parallel);

    /* Verification */
    fprintf(stderr, "[ex1] verifying results...\n");
    int ok = results_match(result_serial, result_parallel, result_size);
    printf("Correctness:   %s\n", ok ? "[OK]" : "[FAIL]");

    free(a);
    free(b);
    free(result_serial);
    free(result_parallel);
    return ok ? 0 : 2;
}