/**
 * @file ex6.c
 * @brief Exercise 1.6 — Parallel top-down mergesort using OpenMP tasks.
 *
 * Implements the classic recursive top-down mergesort. The parallel version
 * uses OpenMP tasks for the two recursive halves. Task creation is controlled
 * with a cutoff threshold so that small subarrays are sorted serially, avoiding
 * excessive task overhead.
 *
 * Usage:
 *   ./ex6 <array_size> <serial|parallel> <num_threads>
 *
 * Example:
 *   ./ex6 10000000 parallel 4
 */

#include <errno.h>
#include <limits.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SEED 42
#define TASK_CUTOFF_DEFAULT 10000
#define MAX_SIZE 200000000
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
 * @brief Parse a string as a positive integer using strtol.
 *
 * @param str   Input string.
 * @param out   Output value on success.
 * @param max   Maximum accepted value.
 * @param label Parameter name for error messages.
 * @return 1 on success, 0 on error.
 */
static int parse_positive_int(const char *str, int *out, int max,
                              const char *label)
{
    if (str == NULL || str[0] == '\0') {
        fprintf(stderr, "Error: %s is empty\n", label);
        return 0;
    }

    char *end = NULL;
    errno = 0;

    long value = strtol(str, &end, 10);

    if (errno == ERANGE || value > INT_MAX || value < INT_MIN) {
        fprintf(stderr, "Error: %s '%s' overflows\n", label, str);
        return 0;
    }

    if (end == str || *end != '\0') {
        fprintf(stderr, "Error: %s '%s' is not a valid integer\n", label, str);
        return 0;
    }

    if (value <= 0) {
        fprintf(stderr, "Error: %s must be positive, got %ld\n", label, value);
        return 0;
    }

    if (value > max) {
        fprintf(stderr, "Error: %s %ld exceeds maximum of %d\n",
                label, value, max);
        return 0;
    }

    *out = (int)value;
    return 1;
}

/**
 * @brief Validate and parse all command-line arguments.
 *
 * @param argc        Argument count.
 * @param argv        Argument vector.
 * @param n           Output array size.
 * @param mode        Output mode string.
 * @param num_threads Output number of threads.
 * @return 1 on success, 0 on error.
 */
static int parse_args(int argc, char *argv[], int *n, const char **mode,
                      int *num_threads)
{
    if (argc != 4) {
        fprintf(stderr,
                "Usage: %s <array_size> <serial|parallel> <num_threads>\n",
                argv[0]);
        return 0;
    }

    if (!parse_positive_int(argv[1], n, MAX_SIZE, "array_size")) {
        return 0;
    }

    *mode = argv[2];

    if (strcmp(*mode, "serial") != 0 && strcmp(*mode, "parallel") != 0) {
        fprintf(stderr,
                "Error: mode must be 'serial' or 'parallel', got '%s'\n",
                *mode);
        return 0;
    }

    if (!parse_positive_int(argv[3], num_threads, MAX_THREADS,
                            "num_threads")) {
        return 0;
    }

    return 1;
}

/**
 * @brief Allocate memory and exit on failure.
 *
 * @param n    Number of elements.
 * @param size Size of each element in bytes.
 * @return Pointer to allocated memory.
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
 * @brief Fill an integer array with deterministic pseudo-random values.
 *
 * @param arr Array to fill.
 * @param n   Array size.
 */
static void fill_random_array(int *arr, int n)
{
    srand(SEED);

    for (int i = 0; i < n; i++) {
        arr[i] = rand();
    }
}

/**
 * @brief Verify that arr[0..n-1] is sorted in non-decreasing order.
 *
 * @param arr Array to check.
 * @param n   Array length.
 * @return 1 if sorted, 0 otherwise.
 */
static int is_sorted(const int *arr, int n)
{
    for (int i = 0; i < n - 1; i++) {
        if (arr[i] > arr[i + 1]) {
            return 0;
        }
    }

    return 1;
}

/**
 * @brief Merge two adjacent sorted subarrays into arr.
 *
 * Merges arr[left..mid] and arr[mid+1..right] using the auxiliary array.
 *
 * @param arr   Array being sorted.
 * @param aux   Auxiliary array.
 * @param left  Start index.
 * @param mid   Middle index.
 * @param right End index.
 */
static void merge(int *arr, int *aux, int left, int mid, int right)
{
    for (int i = left; i <= right; i++) {
        aux[i] = arr[i];
    }

    int i = left;
    int j = mid + 1;
    int k = left;

    while (i <= mid && j <= right) {
        if (aux[i] <= aux[j]) {
            arr[k] = aux[i];
            i++;
        } else {
            arr[k] = aux[j];
            j++;
        }
        k++;
    }

    while (i <= mid) {
        arr[k] = aux[i];
        i++;
        k++;
    }

    while (j <= right) {
        arr[k] = aux[j];
        j++;
        k++;
    }
}

/**
 * @brief Sort arr[left..right] using serial top-down mergesort.
 *
 * @param arr   Array to sort.
 * @param aux   Auxiliary array.
 * @param left  Start index.
 * @param right End index.
 */
static void mergesort_serial(int *arr, int *aux, int left, int right)
{
    if (left >= right) {
        return;
    }

    int mid = left + (right - left) / 2;

    mergesort_serial(arr, aux, left, mid);
    mergesort_serial(arr, aux, mid + 1, right);
    merge(arr, aux, left, mid, right);
}

/**
 * @brief Sort arr[left..right] using OpenMP tasks.
 *
 * Subarrays larger than cutoff are handled with tasks. Smaller subarrays fall
 * back to the serial implementation to avoid excessive task creation overhead.
 *
 * @param arr    Array to sort.
 * @param aux    Auxiliary array.
 * @param left   Start index.
 * @param right  End index.
 * @param cutoff Subarray size below which serial sorting is used.
 */
static void mergesort_parallel(int *arr, int *aux, int left, int right,
                               int cutoff)
{
    if (left >= right) {
        return;
    }

    if ((right - left + 1) <= cutoff) {
        mergesort_serial(arr, aux, left, right);
        return;
    }

    int mid = left + (right - left) / 2;

    #pragma omp task if((right - left + 1) > cutoff) shared(arr, aux)
    mergesort_parallel(arr, aux, left, mid, cutoff);

    #pragma omp task if((right - left + 1) > cutoff) shared(arr, aux)
    mergesort_parallel(arr, aux, mid + 1, right, cutoff);

    #pragma omp taskwait
    merge(arr, aux, left, mid, right);
}

/**
 * @brief Program entry point.
 *
 * @param argc Argument count.
 * @param argv array_size, mode, num_threads.
 * @return 0 on success, 1 on argument error, 2 on correctness failure.
 */
int main(int argc, char *argv[])
{
    int n;
    int num_threads;
    const char *mode;

    if (!parse_args(argc, argv, &n, &mode, &num_threads)) {
        return 1;
    }

    fprintf(stderr, "[ex6] mode=%s size=%d threads=%d cutoff=%d\n",
            mode, n, num_threads, TASK_CUTOFF_DEFAULT);
    fprintf(stderr, "[ex6] generating array...\n");

    int *arr = xmalloc((size_t)n, sizeof(int));
    int *aux = xmalloc((size_t)n, sizeof(int));

    fill_random_array(arr, n);

    fprintf(stderr, "[ex6] sorting...\n");

    double elapsed;

    if (strcmp(mode, "serial") == 0) {
        double t0 = now();
        mergesort_serial(arr, aux, 0, n - 1);
        elapsed = now() - t0;
    } else {
        omp_set_num_threads(num_threads);

        double t0 = now();

        #pragma omp parallel
        {
            #pragma omp single
            mergesort_parallel(arr, aux, 0, n - 1, TASK_CUTOFF_DEFAULT);
        }

        elapsed = now() - t0;
    }

    fprintf(stderr, "[ex6] sort done (%.6f s)\n", elapsed);
    fprintf(stderr, "[ex6] verifying...\n");

    int ok = is_sorted(arr, n);

    printf("Mode:          %s\n", mode);
    printf("Array size:    %d\n", n);
    printf("Threads:       %d\n", num_threads);
    printf("Cutoff:        %d\n", TASK_CUTOFF_DEFAULT);
    printf("Sort time:     %.6f s\n", elapsed);
    printf("Correctness:   %s\n", ok ? "[OK]" : "[FAIL]");

    free(arr);
    free(aux);

    return ok ? 0 : 2;
}