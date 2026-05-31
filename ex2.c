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
#include <time.h>
#include <pthread.h>

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
 * Shared state
 * ---------------------------------------------------------------------- */

/** Shared counter, initialised to zero by main before threads are spawned. */
static long long shared_counter = 0;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;

/* -------------------------------------------------------------------------
 * Data types
 * ---------------------------------------------------------------------- */

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
 */
typedef struct {
    long long iterations; /**< Number of increments this thread performs. */
    sync_mode_t mode;     /**< Which synchronisation primitive to use. */
} counter_args_t;

/* -------------------------------------------------------------------------
 * Worker threads
 * ---------------------------------------------------------------------- */

/**
 * @brief Thread function: increment the shared counter using a mutex.
 *
 * @param arg Pointer to counter_args_t (iterations field used).
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
 * A read-write lock in write mode provides mutual exclusion. Because all
 * operations here are writes, this gives the same correctness guarantees
 * as a mutex but with additional overhead from the rwlock protocol.
 *
 * @param arg Pointer to counter_args_t (iterations field used).
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
 * instruction (e.g. LOCK XADD on x86), avoiding OS-level lock overhead.
 *
 * @param arg Pointer to counter_args_t (iterations field used).
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

/* -------------------------------------------------------------------------
 * Entry point
 * ---------------------------------------------------------------------- */

/**
 * @brief Program entry point.
 *
 * Spawns num_threads threads, each performing the specified number of
 * increments. Prints elapsed time and verifies the final counter value.
 *
 * @param argc Argument count.
 * @param argv Argument vector: num_threads, iterations, mode.
 * @return 0 on success, 1 on argument error.
 */
int main(int argc, char *argv[])
{
    if (argc != 4) {
        fprintf(stderr,
                "Usage: %s <num_threads> <iterations> <mutex|rwlock|atomic>\n",
                argv[0]);
        return 1;
    }

    int num_threads = atoi(argv[1]);
    long long iterations = atoll(argv[2]);
    const char *mode_str = argv[3];

    sync_mode_t mode;
    void *(*worker)(void *);

    if (strcmp(mode_str, "mutex") == 0) {
        mode = MODE_MUTEX;
        worker = worker_mutex;
    } else if (strcmp(mode_str, "rwlock") == 0) {
        mode = MODE_RWLOCK;
        worker = worker_rwlock;
    } else if (strcmp(mode_str, "atomic") == 0) {
        mode = MODE_ATOMIC;
        worker = worker_atomic;
    } else {
        fprintf(stderr, "Unknown mode: %s\n", mode_str);
        return 1;
    }

    pthread_t *threads = malloc((size_t)num_threads * sizeof(pthread_t));
    counter_args_t *args = malloc((size_t)num_threads * sizeof(counter_args_t));

    for (int t = 0; t < num_threads; t++) {
        args[t].iterations = iterations;
        args[t].mode = mode;
    }

    shared_counter = 0;
    double t0 = now();

    for (int t = 0; t < num_threads; t++) {
        pthread_create(&threads[t], NULL, worker, &args[t]);
    }
    for (int t = 0; t < num_threads; t++) {
        pthread_join(threads[t], NULL);
    }

    double elapsed = now() - t0;
    long long expected = (long long)num_threads * iterations;

    printf("Mode:          %s\n", mode_str);
    printf("Threads:       %d\n", num_threads);
    printf("Iterations:    %lld per thread\n", iterations);
    printf("Elapsed:       %.6f s\n", elapsed);
    printf("Counter:       %lld (expected %lld)\n", shared_counter, expected);
    printf("Correctness:   %s\n",
           shared_counter == expected ? "[OK]" : "[FAIL]");

    free(threads);
    free(args);
    pthread_mutex_destroy(&mutex);
    pthread_rwlock_destroy(&rwlock);
    return 0;
}
