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
 *   pthreads   — uses pthread_barrier_t (library baseline)
 *   condvar    — uses a mutex + condition variable (no busy-wait, reusable)
 *   sense      — sense-reversal centralized barrier (spin-wait, reusable)
 *
 * Usage:
 *   ./ex4 <num_threads> <iterations> <pthreads|condvar|sense>
 *
 * Example:
 *   ./ex4 4 100000 sense
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
 * Barrier mode enumeration
 * ---------------------------------------------------------------------- */

/**
 * @brief Which barrier implementation to benchmark.
 */
typedef enum {
    BARRIER_PTHREADS,
    BARRIER_CONDVAR,
    BARRIER_SENSE
} barrier_mode_t;

/* -------------------------------------------------------------------------
 * pthread_barrier — library wrapper
 * ---------------------------------------------------------------------- */

static pthread_barrier_t lib_barrier;

/* -------------------------------------------------------------------------
 * Cond-var barrier (based on pth_cond_bar.c from Pacheco's textbook)
 *
 * Uses a mutex, a condition variable, and a counter. The "sense" phase
 * alternates on each pass so the barrier is safely reusable without a reset.
 * ---------------------------------------------------------------------- */

/**
 * @brief Reusable barrier implemented with a mutex and condition variable.
 */
typedef struct {
    int count;          /**< Number of threads that must arrive. */
    int arrived;        /**< Number of threads that have arrived this phase. */
    int phase;          /**< Current phase (0 or 1); flips each pass. */
    pthread_mutex_t lock;
    pthread_cond_t  cond;
} condvar_barrier_t;

static condvar_barrier_t cv_barrier;

/**
 * @brief Initialise a condvar barrier for use by count threads.
 *
 * @param b     Barrier to initialise.
 * @param count Number of threads that must call condvar_barrier_wait.
 */
static void condvar_barrier_init(condvar_barrier_t *b, int count)
{
    b->count = count;
    b->arrived = 0;
    b->phase = 0;
    pthread_mutex_init(&b->lock, NULL);
    pthread_cond_init(&b->cond, NULL);
}

/**
 * @brief Destroy a condvar barrier, releasing its resources.
 *
 * @param b Barrier to destroy.
 */
static void condvar_barrier_destroy(condvar_barrier_t *b)
{
    pthread_mutex_destroy(&b->lock);
    pthread_cond_destroy(&b->cond);
}

/**
 * @brief Wait at a condvar barrier until all threads have arrived.
 *
 * The last thread to arrive broadcasts and the phase flips so that the
 * barrier is immediately reusable without any reset step.
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
 * Sense-reversal centralized barrier
 *
 * Each thread keeps a thread-local sense flag. The barrier has a shared
 * counter and a global sense flag. On each pass, each thread spins until
 * global_sense matches its local sense, then flips its local sense for
 * the next pass. This makes the barrier reusable while using spin-waiting
 * instead of blocking, which is faster when threads outnumber logical cores.
 * ---------------------------------------------------------------------- */

/**
 * @brief Sense-reversal centralized barrier state.
 */
typedef struct {
    int count;           /**< Total number of participating threads. */
    volatile int arrived; /**< Atomic counter of threads that have arrived. */
    volatile int global_sense; /**< Shared sense flag; flips each pass. */
    pthread_mutex_t count_lock; /**< Protects the arrived counter. */
} sense_barrier_t;

static sense_barrier_t sense_bar;

/** Thread-local sense flag; starts at 1 and flips each pass. */
static __thread int local_sense = 1;

/**
 * @brief Initialise a sense-reversal barrier for use by count threads.
 *
 * @param b     Barrier to initialise.
 * @param count Number of threads that must call sense_barrier_wait.
 */
static void sense_barrier_init(sense_barrier_t *b, int count)
{
    b->count = count;
    b->arrived = 0;
    b->global_sense = 0;
    pthread_mutex_init(&b->count_lock, NULL);
}

/**
 * @brief Destroy a sense-reversal barrier.
 *
 * @param b Barrier to destroy.
 */
static void sense_barrier_destroy(sense_barrier_t *b)
{
    pthread_mutex_destroy(&b->count_lock);
}

/**
 * @brief Wait at a sense-reversal barrier until all threads have arrived.
 *
 * Each thread flips its local_sense before incrementing the shared counter.
 * The last arriving thread resets the counter inside the lock, then flips
 * global_sense outside the lock to release all spinners. Resetting arrived
 * before flipping global_sense ensures the barrier is immediately reusable.
 *
 * @param b Barrier to wait on.
 */
static void sense_barrier_wait(sense_barrier_t *b)
{
    local_sense = 1 - local_sense;

    pthread_mutex_lock(&b->count_lock);
    b->arrived++;
    int all_arrived = (b->arrived == b->count);
    if (all_arrived) {
        b->arrived = 0;
    }
    pthread_mutex_unlock(&b->count_lock);

    if (all_arrived) {
        /* Reset is complete before we publish the release. */
        b->global_sense = local_sense;
    } else {
        while (b->global_sense != local_sense) {
            /* On single-core machines, yield to allow the last thread
             * to reach the barrier. On multi-core, this loop is a
             * busy-spin that resolves without yielding. */
            sched_yield();
        }
    }
}

/* -------------------------------------------------------------------------
 * Thread arguments and worker
 * ---------------------------------------------------------------------- */

/**
 * @brief Arguments passed to each worker thread.
 */
typedef struct {
    int iterations;      /**< Number of barrier passes to perform. */
    barrier_mode_t mode; /**< Which barrier to use. */
} barrier_args_t;

/**
 * @brief Thread worker: loop through N barrier passes.
 *
 * Each iteration calls the appropriate barrier_wait, then continues.
 * The loop body models minimal work so that barrier overhead dominates.
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

/* -------------------------------------------------------------------------
 * Entry point
 * ---------------------------------------------------------------------- */

/**
 * @brief Program entry point.
 *
 * @param argc Argument count.
 * @param argv num_threads, iterations, mode (pthreads|condvar|sense).
 * @return 0 on success, 1 on error.
 */
int main(int argc, char *argv[])
{
    if (argc != 4) {
        fprintf(stderr,
                "Usage: %s <num_threads> <iterations> <pthreads|condvar|sense>\n",
                argv[0]);
        return 1;
    }

    int num_threads = atoi(argv[1]);
    int iterations = atoi(argv[2]);
    const char *mode_str = argv[3];

    barrier_mode_t mode;
    if (strcmp(mode_str, "pthreads") == 0) {
        mode = BARRIER_PTHREADS;
        pthread_barrier_init(&lib_barrier, NULL, (unsigned)num_threads);
    } else if (strcmp(mode_str, "condvar") == 0) {
        mode = BARRIER_CONDVAR;
        condvar_barrier_init(&cv_barrier, num_threads);
    } else if (strcmp(mode_str, "sense") == 0) {
        mode = BARRIER_SENSE;
        sense_barrier_init(&sense_bar, num_threads);
    } else {
        fprintf(stderr, "Unknown mode: %s\n", mode_str);
        return 1;
    }

    pthread_t *threads = malloc((size_t)num_threads * sizeof(pthread_t));
    barrier_args_t *args = malloc((size_t)num_threads * sizeof(barrier_args_t));
    for (int t = 0; t < num_threads; t++) {
        args[t].iterations = iterations;
        args[t].mode = mode;
    }

    double t0 = now();
    for (int t = 0; t < num_threads; t++) {
        pthread_create(&threads[t], NULL, barrier_worker, &args[t]);
    }
    for (int t = 0; t < num_threads; t++) {
        pthread_join(threads[t], NULL);
    }
    double elapsed = now() - t0;

    printf("Mode:          %s\n", mode_str);
    printf("Threads:       %d\n", num_threads);
    printf("Iterations:    %d\n", iterations);
    printf("Elapsed:       %.6f s\n", elapsed);
    printf("Throughput:    %.2f M barrier-passes/s\n",
           (double)num_threads * iterations / elapsed / 1e6);

    /* Cleanup */
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
