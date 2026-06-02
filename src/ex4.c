/* Required for pthread_barrier_t and __thread under strict C11 / POSIX.1-2001 */
#define _XOPEN_SOURCE 600

/**
 * @file ex4.c
 * @brief Exercise 1.4 — Three reusable barrier implementations.
 *
 * All threads run a loop of N iterations. In each iteration they call a
 * barrier_wait, simulating a synchronisation point. Three barrier flavours
 * are selectable via the CLI:
 *
 *   pthreads — uses pthread_barrier_t (library baseline)
 *   condvar  — mutex + condition variable, reusable and blocking
 *   sense    — sense-reversal centralised barrier using atomic operations
 *              and spin waiting
 *
 * Usage:
 *   ./ex4 <num_threads> <iterations> <pthreads|condvar|sense>
 *
 * Example:
 *   ./ex4 4 100000 sense
 */

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_THREADS    256
#define MAX_ITERATIONS 100000000

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
 * @brief Which barrier implementation to benchmark.
 */
typedef enum {
    BARRIER_PTHREADS,
    BARRIER_CONDVAR,
    BARRIER_SENSE
} barrier_mode_t;

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
 * @param num_threads Output thread count.
 * @param iterations  Output iteration count.
 * @param mode_str    Output mode string.
 * @param mode        Output barrier mode.
 * @return 1 on success, 0 on error.
 */
static int parse_args(int argc, char *argv[], int *num_threads, int *iterations,
                      const char **mode_str, barrier_mode_t *mode)
{
    if (argc != 4) {
        fprintf(stderr,
                "Usage: %s <num_threads> <iterations> "
                "<pthreads|condvar|sense>\n",
                argv[0]);
        return 0;
    }

    if (!parse_positive_int(argv[1], num_threads, MAX_THREADS, "num_threads")) {
        return 0;
    }

    if (!parse_positive_int(argv[2], iterations, MAX_ITERATIONS,
                            "iterations")) {
        return 0;
    }

    *mode_str = argv[3];

    if (strcmp(*mode_str, "pthreads") == 0) {
        *mode = BARRIER_PTHREADS;
    } else if (strcmp(*mode_str, "condvar") == 0) {
        *mode = BARRIER_CONDVAR;
    } else if (strcmp(*mode_str, "sense") == 0) {
        *mode = BARRIER_SENSE;
    } else {
        fprintf(stderr,
                "Error: mode must be 'pthreads', 'condvar', or 'sense', "
                "got '%s'\n",
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

    if (ptr == NULL) {
        fprintf(stderr, "Error: malloc failed for %zu bytes\n", n * size);
        exit(1);
    }

    return ptr;
}

static pthread_barrier_t lib_barrier;

/**
 * @brief Reusable barrier implemented with a mutex and condition variable.
 */
typedef struct {
    int count;
    int arrived;
    int phase;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} condvar_barrier_t;

static condvar_barrier_t cv_barrier;

/**
 * @brief Initialise a condition-variable barrier.
 *
 * @param b     Barrier to initialise.
 * @param count Number of participating threads.
 */
static void condvar_barrier_init(condvar_barrier_t *b, int count)
{
    b->count = count;
    b->arrived = 0;
    b->phase = 0;

    int rc = pthread_mutex_init(&b->lock, NULL);
    if (rc != 0) {
        fprintf(stderr, "Error: pthread_mutex_init failed: %s\n", strerror(rc));
        exit(1);
    }

    rc = pthread_cond_init(&b->cond, NULL);
    if (rc != 0) {
        fprintf(stderr, "Error: pthread_cond_init failed: %s\n", strerror(rc));
        exit(1);
    }
}

/**
 * @brief Destroy a condition-variable barrier.
 *
 * @param b Barrier to destroy.
 */
static void condvar_barrier_destroy(condvar_barrier_t *b)
{
    pthread_mutex_destroy(&b->lock);
    pthread_cond_destroy(&b->cond);
}

/**
 * @brief Wait at a reusable condition-variable barrier.
 *
 * @param b Barrier to wait on.
 */
static void condvar_barrier_wait(condvar_barrier_t *b)
{
    pthread_mutex_lock(&b->lock);

    int my_phase = b->phase;
    b->arrived++;

    if (b->arrived == b->count) {
        b->arrived = 0;
        b->phase = 1 - b->phase;
        pthread_cond_broadcast(&b->cond);
    } else {
        while (b->phase == my_phase) {
            pthread_cond_wait(&b->cond, &b->lock);
        }
    }

    pthread_mutex_unlock(&b->lock);
}

/* -------------------------------------------------------------------------
 * Atomic sense-reversal centralised barrier
 *
 * Each thread keeps a thread-local sense flag. On each barrier pass, the
 * thread flips its local sense and atomically decrements the shared counter.
 * The last thread to arrive resets the counter and publishes the new global
 * sense, releasing all spinning threads.
 *
 * The local/global sense values make the barrier reusable because each pass
 * waits for a different generation value than the previous pass.
 * ---------------------------------------------------------------------- */

/**
 * @brief Sense-reversal centralised barrier state.
 */
typedef struct {
    int num_threads;
    int count;
    int global_sense;
} sense_barrier_t;

static sense_barrier_t sense_bar;

/** Thread-local sense flag. Starts at 0 so the first pass waits for 1. */
static __thread int local_sense = 0;

/**
 * @brief Initialise a sense-reversal barrier.
 *
 * @param b     Barrier to initialise.
 * @param count Number of participating threads.
 */
static void sense_barrier_init(sense_barrier_t *b, int count)
{
    b->num_threads = count;
    __atomic_store_n(&b->count, count, __ATOMIC_RELAXED);
    __atomic_store_n(&b->global_sense, 0, __ATOMIC_RELAXED);
}

/**
 * @brief Destroy a sense-reversal barrier.
 *
 * @param b Barrier to destroy.
 */
static void sense_barrier_destroy(sense_barrier_t *b)
{
    (void)b;
}

/**
 * @brief Wait at an atomic sense-reversal barrier.
 *
 * @param b Barrier to wait on.
 */
static void sense_barrier_wait(sense_barrier_t *b)
{
    local_sense = 1 - local_sense;

    int old_count = __atomic_fetch_sub(&b->count, 1, __ATOMIC_ACQ_REL);

    if (old_count == 1) {
        __atomic_store_n(&b->count, b->num_threads, __ATOMIC_RELEASE);
        __atomic_store_n(&b->global_sense, local_sense, __ATOMIC_RELEASE);
    } else {
        while (__atomic_load_n(&b->global_sense, __ATOMIC_ACQUIRE)
               != local_sense) {
            sched_yield();
        }
    }
}

/**
 * @brief Arguments passed to each worker thread.
 */
typedef struct {
    int iterations;
    barrier_mode_t mode;
} barrier_args_t;

/**
 * @brief Thread worker: execute repeated barrier waits.
 *
 * @param arg Pointer to barrier_args_t.
 * @return NULL always.
 */
static void *barrier_worker(void *arg)
{
    barrier_args_t *args = (barrier_args_t *)arg;

    for (int i = 0; i < args->iterations; i++) {
        switch (args->mode) {
        case BARRIER_PTHREADS:
            pthread_barrier_wait(&lib_barrier);
            break;
        case BARRIER_CONDVAR:
            condvar_barrier_wait(&cv_barrier);
            break;
        case BARRIER_SENSE:
            sense_barrier_wait(&sense_bar);
            break;
        }
    }

    return NULL;
}

/**
 * @brief Program entry point.
 *
 * @param argc Argument count.
 * @param argv num_threads, iterations, mode.
 * @return 0 on success, 1 on error.
 */
int main(int argc, char *argv[])
{
    int num_threads;
    int iterations;
    const char *mode_str;
    barrier_mode_t mode;

    if (!parse_args(argc, argv, &num_threads, &iterations, &mode_str, &mode)) {
        return 1;
    }

    if (mode == BARRIER_PTHREADS) {
        int rc = pthread_barrier_init(&lib_barrier, NULL,
                                      (unsigned)num_threads);
        if (rc != 0) {
            fprintf(stderr, "Error: pthread_barrier_init failed: %s\n",
                    strerror(rc));
            return 1;
        }
    } else if (mode == BARRIER_CONDVAR) {
        condvar_barrier_init(&cv_barrier, num_threads);
    } else {
        sense_barrier_init(&sense_bar, num_threads);
    }

    fprintf(stderr, "[ex4] mode=%s threads=%d iterations=%d\n",
            mode_str, num_threads, iterations);
    fprintf(stderr, "[ex4] spawning threads...\n");

    pthread_t *threads = xmalloc((size_t)num_threads, sizeof(pthread_t));
    barrier_args_t *args = xmalloc((size_t)num_threads,
                                   sizeof(barrier_args_t));

    for (int t = 0; t < num_threads; t++) {
        args[t].iterations = iterations;
        args[t].mode = mode;
    }

    double t0 = now();

    for (int t = 0; t < num_threads; t++) {
        int rc = pthread_create(&threads[t], NULL, barrier_worker, &args[t]);
        if (rc != 0) {
            fprintf(stderr, "Error: pthread_create failed for thread %d: %s\n",
                    t, strerror(rc));
            free(threads);
            free(args);
            return 1;
        }
    }

    for (int t = 0; t < num_threads; t++) {
        int rc = pthread_join(threads[t], NULL);
        if (rc != 0) {
            fprintf(stderr, "Error: pthread_join failed for thread %d: %s\n",
                    t, strerror(rc));
            free(threads);
            free(args);
            return 1;
        }
    }

    double elapsed = now() - t0;

    fprintf(stderr, "[ex4] done (%.6f s)\n", elapsed);

    printf("Mode:          %s\n", mode_str);
    printf("Threads:       %d\n", num_threads);
    printf("Iterations:    %d\n", iterations);
    printf("Elapsed:       %.6f s\n", elapsed);
    printf("Throughput:    %.2f M barrier-passes/s\n",
           (double)num_threads * iterations / elapsed / 1e6);

    if (mode == BARRIER_PTHREADS) {
        pthread_barrier_destroy(&lib_barrier);
    } else if (mode == BARRIER_CONDVAR) {
        condvar_barrier_destroy(&cv_barrier);
    } else {
        sense_barrier_destroy(&sense_bar);
    }

    free(threads);
    free(args);

    return 0;
}
