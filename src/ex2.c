/* Required for pthread_rwlock_t under strict C11 / POSIX.1-2001 */
#define _XOPEN_SOURCE 600

/**
 * @file ex2.c
 * @brief Exercise 1.2 — Shared counter with three synchronisation approaches.
 *
 * All threads increment a shared variable in a tight loop. Three approaches
 * are compared: mutual-exclusion lock, read-write lock (write mode), and
 * GCC built-in atomic fetch-and-add.
 *
 * Usage:
 *   ./ex2 <num_threads> <iterations> <mutex|rwlock|atomic>
 *
 * Example:
 *   ./ex2 4 1000000 atomic
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <pthread.h>

#define MAX_THREADS    256
#define MAX_ITERATIONS 1000000000LL

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

/** Shared counter, reset to zero by main before each run. */
static long long shared_counter = 0;

static pthread_mutex_t  mutex  = PTHREAD_MUTEX_INITIALIZER;
static pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;

/**
 * @brief Synchronisation strategies available to the worker threads.
 */
typedef enum {
    MODE_MUTEX,
    MODE_RWLOCK,
    MODE_ATOMIC
} sync_mode_t;

/**
 * @brief Arguments passed to each worker thread.
 *
 * The mode field has been removed: worker selection happens at dispatch
 * time in main via a function pointer, so each worker function already
 * knows which primitive to use without carrying it as a field.
 */
typedef struct {
    long long iterations; /**< Number of increments this thread performs. */
} counter_args_t;

/**
 * @brief Parse a string as a positive integer using strtol.
 *
 * Rejects empty strings, non-numeric input, trailing garbage, values that
 * overflow long, negative values, zero, and values exceeding max.
 *
 * @param str   Input string to parse.
 * @param out   Output: parsed value on success.
 * @param max   Maximum accepted value (inclusive).
 * @param label Parameter name, used in error messages.
 * @return 1 on success, 0 on any error.
 */
static int parse_positive_long(const char *str, long long *out, long long max,
                               const char *label)
{
    if (str == NULL || str[0] == '\0') {
        fprintf(stderr, "Error: %s is empty\n", label);
        return 0;
    }

    char *end;
    errno = 0;
    long long val = strtoll(str, &end, 10);

    if (errno == ERANGE) {
        fprintf(stderr, "Error: %s '%s' overflows\n", label, str);
        return 0;
    }
    if (end == str || *end != '\0') {
        fprintf(stderr, "Error: %s '%s' is not a valid integer\n", label, str);
        return 0;
    }
    if (val <= 0) {
        fprintf(stderr, "Error: %s must be positive, got %lld\n", label, val);
        return 0;
    }
    if (val > max) {
        fprintf(stderr, "Error: %s %lld exceeds maximum of %lld\n",
                label, val, max);
        return 0;
    }

    *out = val;
    return 1;
}

/**
 * @brief Validate and parse all command-line arguments.
 *
 * @param argc        Argument count from main.
 * @param argv        Argument vector from main.
 * @param num_threads Output: parsed thread count.
 * @param iterations  Output: parsed iteration count.
 * @param mode_str    Output: pointer into argv for the mode string.
 * @return 1 if all arguments are valid, 0 otherwise.
 */
static int parse_args(int argc, char *argv[], int *num_threads,
                      long long *iterations, const char **mode_str)
{
    if (argc != 4) {
        fprintf(stderr,
                "Usage: %s <num_threads> <iterations> <mutex|rwlock|atomic>\n",
                argv[0]);
        return 0;
    }

    long long t_val;
    if (!parse_positive_long(argv[1], &t_val, MAX_THREADS, "num_threads")) {
        return 0;
    }
    *num_threads = (int)t_val;

    if (!parse_positive_long(argv[2], iterations, MAX_ITERATIONS, "iterations")) {
        return 0;
    }

    *mode_str = argv[3];
    if (strcmp(*mode_str, "mutex")  != 0 &&
        strcmp(*mode_str, "rwlock") != 0 &&
        strcmp(*mode_str, "atomic") != 0) {
        fprintf(stderr,
                "Error: mode must be 'mutex', 'rwlock', or 'atomic', got '%s'\n",
                *mode_str);
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
    if (!ptr) {
        fprintf(stderr, "Error: malloc failed for %zu bytes\n", n * size);
        exit(1);
    }
    return ptr;
}

/**
 * @brief Thread function: increment the shared counter using a mutex.
 *
 * @param arg Pointer to counter_args_t.
 * @return NULL always.
 */
static void *worker_mutex(void *arg)
{
    counter_args_t *args = (counter_args_t *)arg;
    for (long long i = 0; i < args->iterations; i++) {
        pthread_mutex_lock(&mutex);
        shared_counter++;
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

/**
 * @brief Thread function: increment the shared counter using a write-lock.
 *
 * A read-write lock in write mode provides mutual exclusion identical to a
 * mutex. Because all operations here are writes, readers never share the
 * lock and the additional bookkeeping in the rwlock protocol makes this
 * strictly slower than a plain mutex for this workload.
 *
 * @param arg Pointer to counter_args_t.
 * @return NULL always.
 */
static void *worker_rwlock(void *arg)
{
    counter_args_t *args = (counter_args_t *)arg;
    for (long long i = 0; i < args->iterations; i++) {
        pthread_rwlock_wrlock(&rwlock);
        shared_counter++;
        pthread_rwlock_unlock(&rwlock);
    }
    return NULL;
}

/**
 * @brief Thread function: increment the shared counter using a GCC atomic.
 *
 * __atomic_fetch_add with __ATOMIC_SEQ_CST maps to a single hardware
 * instruction (e.g. LOCK XADD on x86), avoiding OS-level lock overhead
 * entirely.
 *
 * @param arg Pointer to counter_args_t.
 * @return NULL always.
 */
static void *worker_atomic(void *arg)
{
    counter_args_t *args = (counter_args_t *)arg;
    for (long long i = 0; i < args->iterations; i++) {
        __atomic_fetch_add(&shared_counter, 1LL, __ATOMIC_SEQ_CST);
    }
    return NULL;
}

/**
 * @brief Program entry point.
 *
 * Spawns num_threads threads each performing iterations increments on a
 * shared counter. Prints elapsed time and verifies the final value.
 *
 * @param argc Argument count.
 * @param argv num_threads, iterations, mode (mutex|rwlock|atomic).
 * @return 0 on success, 1 on argument error, 2 on correctness failure.
 */
int main(int argc, char *argv[])
{
    int num_threads;
    long long iterations;
    const char *mode_str;

    if (!parse_args(argc, argv, &num_threads, &iterations, &mode_str)) {
        return 1;
    }

    void *(*worker)(void *);
    if (strcmp(mode_str, "mutex") == 0) {
        worker = worker_mutex;
    } else if (strcmp(mode_str, "rwlock") == 0) {
        worker = worker_rwlock;
    } else {
        worker = worker_atomic;
    }

    fprintf(stderr, "[ex2] mode=%s threads=%d iterations=%lld\n",
            mode_str, num_threads, iterations);
    fprintf(stderr, "[ex2] spawning threads...\n");

    pthread_t      *threads = xmalloc((size_t)num_threads, sizeof(pthread_t));
    counter_args_t *args    = xmalloc((size_t)num_threads, sizeof(counter_args_t));

    for (int t = 0; t < num_threads; t++) {
        args[t].iterations = iterations;
    }

    shared_counter = 0;
    double t0 = now();

    for (int t = 0; t < num_threads; t++) {
        int rc = pthread_create(&threads[t], NULL, worker, &args[t]);
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

    double elapsed = now() - t0;
    long long expected = (long long)num_threads * iterations;

    fprintf(stderr, "[ex2] done (%.6f s)\n", elapsed);

    printf("Mode:          %s\n", mode_str);
    printf("Threads:       %d\n", num_threads);
    printf("Iterations:    %lld per thread\n", iterations);
    printf("Elapsed:       %.6f s\n", elapsed);
    printf("Counter:       %lld (expected %lld)\n", shared_counter, expected);

    int ok = (shared_counter == expected);
    printf("Correctness:   %s\n", ok ? "[OK]" : "[FAIL]");

    free(threads);
    free(args);
    pthread_mutex_destroy(&mutex);
    pthread_rwlock_destroy(&rwlock);
    return ok ? 0 : 2;
}