/* Required for pthread_rwlock_t under strict C11 / POSIX.1-2001 */
#define _XOPEN_SOURCE 600

/**
 * @file ex3.c
 * @brief Exercise 1.3 — Bank simulation with configurable locking granularity.
 *
 * Simulates a shared array of bank accounts accessed by multiple threads.
 * Each thread performs a fixed number of transactions, which are either
 * money transfers (read-modify-write on two accounts) or balance queries
 * (read of one account). Four locking schemes are compared:
 *
 *   coarse_mutex  — one global pthread_mutex_t
 *   fine_mutex    — one pthread_mutex_t per account (low-to-high ordering)
 *   coarse_rw     — one global pthread_rwlock_t
 *   fine_rw       — one pthread_rwlock_t per account
 *
 * Usage:
 *   ./ex3 <num_accounts> <transactions_per_thread> <read_pct>
 *         <coarse_mutex|fine_mutex|coarse_rw|fine_rw> <num_threads>
 *
 * read_pct is an integer in [0,100] controlling the percentage of balance
 * queries among all transactions.
 *
 * Example:
 *   ./ex3 1000 10000 80 fine_rw 4
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <math.h>

#define SEED        42
#define MAX_AMOUNT  100
#define SLEEP_US    10  /**< Microseconds to sleep inside extended read section. */

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
 * Locking scheme enumeration
 * ---------------------------------------------------------------------- */

/**
 * @brief Available synchronisation strategies.
 */
typedef enum {
    SCHEME_COARSE_MUTEX,
    SCHEME_FINE_MUTEX,
    SCHEME_COARSE_RW,
    SCHEME_FINE_RW
} lock_scheme_t;

/* -------------------------------------------------------------------------
 * Shared bank state
 * ---------------------------------------------------------------------- */

static int *accounts;            /**< Array of account balances. */
static int num_accounts;         /**< Total number of accounts. */
static long long initial_total;  /**< Sum of balances at initialisation. */

static pthread_mutex_t  coarse_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_rwlock_t coarse_rw    = PTHREAD_RWLOCK_INITIALIZER;
static pthread_mutex_t  *fine_mutexes;  /**< Per-account mutex array. */
static pthread_rwlock_t *fine_rwlocks; /**< Per-account rwlock array. */

/* -------------------------------------------------------------------------
 * Transaction implementations — one set per locking scheme
 * ---------------------------------------------------------------------- */

/**
 * @brief Transfer amount from src to dst using the coarse mutex.
 *
 * @param src    Source account index.
 * @param dst    Destination account index.
 * @param amount Amount to transfer.
 */
static void transfer_coarse_mutex(int src, int dst, int amount)
{
    pthread_mutex_lock(&coarse_mutex);
    accounts[src] -= amount;
    accounts[dst] += amount;
    pthread_mutex_unlock(&coarse_mutex);
}

/**
 * @brief Read balance of account idx using the coarse mutex.
 *
 * Also performs a small math operation to model a non-trivial read section.
 *
 * @param idx    Account index to query.
 * @param accum  Accumulator for the running balance sum (private to thread).
 */
static void query_coarse_mutex(int idx, long long *accum)
{
    pthread_mutex_lock(&coarse_mutex);
    int balance = accounts[idx];
    *accum += (long long)sqrt((double)abs(balance) + 1.0);
    pthread_mutex_unlock(&coarse_mutex);
}

/**
 * @brief Transfer amount from src to dst using per-account mutexes.
 *
 * Locks are always acquired in ascending index order to prevent deadlock.
 *
 * @param src    Source account index.
 * @param dst    Destination account index.
 * @param amount Amount to transfer.
 */
static void transfer_fine_mutex(int src, int dst, int amount)
{
    int first = src < dst ? src : dst;
    int second = src < dst ? dst : src;
    pthread_mutex_lock(&fine_mutexes[first]);
    pthread_mutex_lock(&fine_mutexes[second]);
    accounts[src] -= amount;
    accounts[dst] += amount;
    pthread_mutex_unlock(&fine_mutexes[second]);
    pthread_mutex_unlock(&fine_mutexes[first]);
}

/**
 * @brief Read balance of account idx using the per-account mutex.
 *
 * @param idx   Account index to query.
 * @param accum Accumulator for the running balance sum (private to thread).
 */
static void query_fine_mutex(int idx, long long *accum)
{
    pthread_mutex_lock(&fine_mutexes[idx]);
    int balance = accounts[idx];
    *accum += (long long)sqrt((double)abs(balance) + 1.0);
    pthread_mutex_unlock(&fine_mutexes[idx]);
}

/**
 * @brief Transfer amount from src to dst using the coarse rwlock.
 *
 * Money transfer always requires a write lock since two accounts are modified.
 *
 * @param src    Source account index.
 * @param dst    Destination account index.
 * @param amount Amount to transfer.
 */
static void transfer_coarse_rw(int src, int dst, int amount)
{
    pthread_rwlock_wrlock(&coarse_rw);
    accounts[src] -= amount;
    accounts[dst] += amount;
    pthread_rwlock_unlock(&coarse_rw);
}

/**
 * @brief Read balance of account idx using the coarse rwlock in read mode.
 *
 * Multiple threads may hold this lock simultaneously, which reduces
 * contention when read_pct is high.
 *
 * @param idx   Account index to query.
 * @param accum Accumulator for the running balance sum (private to thread).
 */
static void query_coarse_rw(int idx, long long *accum)
{
    pthread_rwlock_rdlock(&coarse_rw);
    int balance = accounts[idx];
    *accum += (long long)sqrt((double)abs(balance) + 1.0);
    pthread_rwlock_unlock(&coarse_rw);
}

/**
 * @brief Transfer amount from src to dst using per-account rwlocks.
 *
 * Both affected accounts are write-locked in ascending index order.
 *
 * @param src    Source account index.
 * @param dst    Destination account index.
 * @param amount Amount to transfer.
 */
static void transfer_fine_rw(int src, int dst, int amount)
{
    int first = src < dst ? src : dst;
    int second = src < dst ? dst : src;
    pthread_rwlock_wrlock(&fine_rwlocks[first]);
    pthread_rwlock_wrlock(&fine_rwlocks[second]);
    accounts[src] -= amount;
    accounts[dst] += amount;
    pthread_rwlock_unlock(&fine_rwlocks[second]);
    pthread_rwlock_unlock(&fine_rwlocks[first]);
}

/**
 * @brief Read balance of account idx using the per-account rwlock in read mode.
 *
 * @param idx   Account index to query.
 * @param accum Accumulator for the running balance sum (private to thread).
 */
static void query_fine_rw(int idx, long long *accum)
{
    pthread_rwlock_rdlock(&fine_rwlocks[idx]);
    int balance = accounts[idx];
    *accum += (long long)sqrt((double)abs(balance) + 1.0);
    pthread_rwlock_unlock(&fine_rwlocks[idx]);
}

/* -------------------------------------------------------------------------
 * Thread arguments and worker
 * ---------------------------------------------------------------------- */

/**
 * @brief Arguments passed to each bank simulation thread.
 */
typedef struct {
    int transactions; /**< Total number of transactions to perform. */
    int read_pct;     /**< Percentage [0,100] of transactions that are reads. */
    lock_scheme_t scheme; /**< Which locking scheme to use. */
    unsigned int seed;    /**< Per-thread RNG seed for reproducibility. */
} bank_args_t;

/**
 * @brief Thread worker: perform the assigned number of bank transactions.
 *
 * Randomly selects transaction type (transfer or query) based on read_pct.
 * The balance query accumulator is kept private to avoid shared writes.
 *
 * @param arg Pointer to bank_args_t.
 * @return NULL always.
 */
static void *bank_worker(void *arg)
{
    bank_args_t *args = (bank_args_t *)arg;
    unsigned int seed = args->seed;
    long long local_sum = 0;

    for (int t = 0; t < args->transactions; t++) {
        int is_read = (rand_r(&seed) % 100) < args->read_pct;

        if (is_read) {
            int idx = rand_r(&seed) % num_accounts;
            switch (args->scheme) {
            case SCHEME_COARSE_MUTEX: query_coarse_mutex(idx, &local_sum); break;
            case SCHEME_FINE_MUTEX:   query_fine_mutex(idx, &local_sum);   break;
            case SCHEME_COARSE_RW:    query_coarse_rw(idx, &local_sum);    break;
            case SCHEME_FINE_RW:      query_fine_rw(idx, &local_sum);      break;
            }
        } else {
            int src = rand_r(&seed) % num_accounts;
            int dst;
            do {
                dst = rand_r(&seed) % num_accounts;
            } while (dst == src);
            int amount = (rand_r(&seed) % MAX_AMOUNT) + 1;
            switch (args->scheme) {
            case SCHEME_COARSE_MUTEX: transfer_coarse_mutex(src, dst, amount); break;
            case SCHEME_FINE_MUTEX:   transfer_fine_mutex(src, dst, amount);   break;
            case SCHEME_COARSE_RW:    transfer_coarse_rw(src, dst, amount);    break;
            case SCHEME_FINE_RW:      transfer_fine_rw(src, dst, amount);      break;
            }
        }
    }
    return NULL;
}

/* -------------------------------------------------------------------------
 * Verification
 * ---------------------------------------------------------------------- */

/**
 * @brief Verify that the total sum of all account balances is unchanged.
 *
 * Money transfers preserve the total sum; any discrepancy indicates a race.
 *
 * @return 1 if the total is preserved, 0 otherwise.
 */
static int verify_total(void)
{
    long long total = 0;
    for (int i = 0; i < num_accounts; i++) {
        total += accounts[i];
    }
    return total == initial_total;
}

/* -------------------------------------------------------------------------
 * Entry point
 * ---------------------------------------------------------------------- */

/**
 * @brief Program entry point.
 *
 * @param argc Argument count.
 * @param argv num_accounts, transactions_per_thread, read_pct, scheme,
 *             num_threads.
 * @return 0 on success, 1 on error.
 */
int main(int argc, char *argv[])
{
    if (argc != 6) {
        fprintf(stderr,
                "Usage: %s <num_accounts> <transactions_per_thread> "
                "<read_pct> <coarse_mutex|fine_mutex|coarse_rw|fine_rw> "
                "<num_threads>\n",
                argv[0]);
        return 1;
    }

    num_accounts = atoi(argv[1]);
    int transactions = atoi(argv[2]);
    int read_pct = atoi(argv[3]);
    const char *scheme_str = argv[4];
    int num_threads = atoi(argv[5]);

    lock_scheme_t scheme;
    if (strcmp(scheme_str, "coarse_mutex") == 0) {
        scheme = SCHEME_COARSE_MUTEX;
    } else if (strcmp(scheme_str, "fine_mutex") == 0) {
        scheme = SCHEME_FINE_MUTEX;
    } else if (strcmp(scheme_str, "coarse_rw") == 0) {
        scheme = SCHEME_COARSE_RW;
    } else if (strcmp(scheme_str, "fine_rw") == 0) {
        scheme = SCHEME_FINE_RW;
    } else {
        fprintf(stderr, "Unknown scheme: %s\n", scheme_str);
        return 1;
    }

    /* Initialise accounts */
    srand(SEED);
    accounts = malloc((size_t)num_accounts * sizeof(int));
    initial_total = 0;
    for (int i = 0; i < num_accounts; i++) {
        accounts[i] = (rand() % 10000) + 1;
        initial_total += accounts[i];
    }

    /* Initialise per-account locks if needed */
    if (scheme == SCHEME_FINE_MUTEX) {
        fine_mutexes = malloc((size_t)num_accounts * sizeof(pthread_mutex_t));
        for (int i = 0; i < num_accounts; i++) {
            pthread_mutex_init(&fine_mutexes[i], NULL);
        }
    }
    if (scheme == SCHEME_FINE_RW) {
        fine_rwlocks = malloc((size_t)num_accounts * sizeof(pthread_rwlock_t));
        for (int i = 0; i < num_accounts; i++) {
            pthread_rwlock_init(&fine_rwlocks[i], NULL);
        }
    }

    /* Spawn threads */
    pthread_t *threads = malloc((size_t)num_threads * sizeof(pthread_t));
    bank_args_t *args = malloc((size_t)num_threads * sizeof(bank_args_t));

    double t0 = now();
    for (int t = 0; t < num_threads; t++) {
        args[t].transactions = transactions;
        args[t].read_pct = read_pct;
        args[t].scheme = scheme;
        args[t].seed = (unsigned int)(SEED + t);
        pthread_create(&threads[t], NULL, bank_worker, &args[t]);
    }
    for (int t = 0; t < num_threads; t++) {
        pthread_join(threads[t], NULL);
    }
    double elapsed = now() - t0;

    printf("Scheme:        %s\n", scheme_str);
    printf("Threads:       %d\n", num_threads);
    printf("Accounts:      %d\n", num_accounts);
    printf("Transactions:  %d per thread\n", transactions);
    printf("Read pct:      %d%%\n", read_pct);
    printf("Elapsed:       %.6f s\n", elapsed);
    printf("Correctness:   %s\n", verify_total() ? "[OK]" : "[FAIL]");

    /* Cleanup */
    if (scheme == SCHEME_FINE_MUTEX) {
        for (int i = 0; i < num_accounts; i++) {
            pthread_mutex_destroy(&fine_mutexes[i]);
        }
        free(fine_mutexes);
    }
    if (scheme == SCHEME_FINE_RW) {
        for (int i = 0; i < num_accounts; i++) {
            pthread_rwlock_destroy(&fine_rwlocks[i]);
        }
        free(fine_rwlocks);
    }
    pthread_mutex_destroy(&coarse_mutex);
    pthread_rwlock_destroy(&coarse_rw);
    free(accounts);
    free(threads);
    free(args);
    return 0;
}
