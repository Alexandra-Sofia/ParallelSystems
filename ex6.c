/**
 * @file ex6.c
 * @brief Exercise 1.6 — Parallel top-down mergesort using OpenMP tasks.
 *
 * Implements the classic recursive mergesort. The parallel version spawns
 * an OpenMP task for each recursive half, gated by a size threshold. When
 * the sub-array is smaller than the threshold, the serial path is taken to
 * avoid task-creation overhead on trivially small inputs.
 *
 * Usage:
 *   ./ex6 <array_size> <serial|parallel> <num_threads>
 *
 * Example:
 *   ./ex6 10000000 parallel 4
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <omp.h>

#define SEED              42
#define TASK_CUTOFF_DEFAULT 10000

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
 * Merge step (shared by serial and parallel)
 * ---------------------------------------------------------------------- */

/**
 * @brief Merge two adjacent sorted sub-arrays into a temporary buffer.
 *
 * Merges arr[left..mid] and arr[mid+1..right] in-place using a temporary
 * heap allocation.
 *
 * @param arr  Array to merge within.
 * @param left Start index of the left half (inclusive).
 * @param mid  End index of the left half (inclusive).
 * @param right End index of the right half (inclusive).
 */
static void merge(int *arr, int left, int mid, int right)
{
    int len_left = mid - left + 1;
    int len_right = right - mid;
    int *tmp = malloc(((size_t)len_left + (size_t)len_right) * sizeof(int));

    memcpy(tmp, arr + left, (size_t)len_left * sizeof(int));
    memcpy(tmp + len_left, arr + mid + 1, (size_t)len_right * sizeof(int));

    int i = 0, j = len_left, k = left;
    while (i < len_left && j < (len_left + len_right)) {
        if (tmp[i] <= tmp[j]) {
            arr[k++] = tmp[i++];
        } else {
            arr[k++] = tmp[j++];
        }
    }
    while (i < len_left) {
        arr[k++] = tmp[i++];
    }
    while (j < (len_left + len_right)) {
        arr[k++] = tmp[j++];
    }
    free(tmp);
}

/* -------------------------------------------------------------------------
 * Serial mergesort
 * ---------------------------------------------------------------------- */

/**
 * @brief Sort arr[left..right] using the serial top-down mergesort algorithm.
 *
 * @param arr   Array to sort.
 * @param left  Start index (inclusive).
 * @param right End index (inclusive).
 */
static void mergesort_serial(int *arr, int left, int right)
{
    if (left >= right) {
        return;
    }
    int mid = left + (right - left) / 2;
    mergesort_serial(arr, left, mid);
    mergesort_serial(arr, mid + 1, right);
    merge(arr, left, mid, right);
}

/* -------------------------------------------------------------------------
 * Parallel mergesort with OpenMP tasks
 * ---------------------------------------------------------------------- */

/**
 * @brief Sort arr[left..right] using OpenMP tasks for parallelism.
 *
 * Recursion spawns two tasks for the two halves when the sub-array size
 * exceeds TASK_CUTOFF. Below the cutoff, falls back to the serial path to
 * avoid task-creation overhead on small inputs. The if() clause on the task
 * directive achieves this more concisely and lets OpenMP decide task vs
 * inline execution based on the runtime condition.
 *
 * @param arr      Array to sort.
 * @param left     Start index (inclusive).
 * @param right    End index (inclusive).
 * @param cutoff   Sub-array size below which the serial path is taken.
 */
static void mergesort_parallel(int *arr, int left, int right, int cutoff)
{
    if (left >= right) {
        return;
    }
    if ((right - left + 1) <= cutoff) {
        mergesort_serial(arr, left, right);
        return;
    }

    int mid = left + (right - left) / 2;

    #pragma omp task if((right - left + 1) > cutoff)
    mergesort_parallel(arr, left, mid, cutoff);

    #pragma omp task if((right - left + 1) > cutoff)
    mergesort_parallel(arr, mid + 1, right, cutoff);

    #pragma omp taskwait
    merge(arr, left, mid, right);
}

/* -------------------------------------------------------------------------
 * Verification
 * ---------------------------------------------------------------------- */

/**
 * @brief Verify that arr[0..n-1] is sorted in non-decreasing order.
 *
 * @param arr Array to check.
 * @param n   Length of the array.
 * @return 1 if sorted, 0 otherwise.
 */
static int is_sorted(int *arr, int n)
{
    for (int i = 0; i < n - 1; i++) {
        if (arr[i] > arr[i + 1]) {
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
 * @param argv array_size, mode (serial|parallel), num_threads.
 * @return 0 on success, 1 on error.
 */
int main(int argc, char *argv[])
{
    if (argc != 4) {
        fprintf(stderr,
                "Usage: %s <array_size> <serial|parallel> <num_threads>\n",
                argv[0]);
        return 1;
    }

    int n = atoi(argv[1]);
    const char *mode = argv[2];
    int num_threads = atoi(argv[3]);

    /* Generate a deterministic random array */
    int *arr = malloc((size_t)n * sizeof(int));
    srand(SEED);
    for (int i = 0; i < n; i++) {
        arr[i] = rand();
    }

    double elapsed;

    if (strcmp(mode, "serial") == 0) {
        double t0 = now();
        mergesort_serial(arr, 0, n - 1);
        elapsed = now() - t0;
    } else if (strcmp(mode, "parallel") == 0) {
        omp_set_num_threads(num_threads);
        double t0 = now();
        #pragma omp parallel
        {
            #pragma omp single
            mergesort_parallel(arr, 0, n - 1, TASK_CUTOFF_DEFAULT);
        }
        elapsed = now() - t0;
    } else {
        fprintf(stderr, "Unknown mode: %s\n", mode);
        free(arr);
        return 1;
    }

    printf("Mode:          %s\n", mode);
    printf("Array size:    %d\n", n);
    printf("Threads:       %d\n", num_threads);
    printf("Sort time:     %.6f s\n", elapsed);
    printf("Correctness:   %s\n", is_sorted(arr, n) ? "[OK]" : "[FAIL]");

    free(arr);
    return 0;
}
