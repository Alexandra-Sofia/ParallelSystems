/* Required for pthread_rwlock_t under strict C11 / POSIX.1-2001 */
#define _XOPEN_SOURCE 600

/**
 * @file ex3.c
 * @brief Exercise 1.3 — Bank simulation with configurable locking granularity.
 *
 * Simulates a shared array of bank accounts accessed by multiple threads.
 * Each thread performs a fixed number of transactions, either money transfers
 * (read-modify-write on two accounts) or balance queries (read of one
 * account). Four locking schemes are compared:
 *
 *   coarse_mutex  — one global pthread_mutex_t
 *   fine_mutex    — one pthread_mutex_t per account (low-to-high ordering)
 *   coarse_rw     — one global pthread_rwlock_t
 *   fine_rw       — one pthread_rwlock_t per account
 *
 * The balance query critical section can be extended via read_work_iters to
 * make the rwlock advantage more observable at high read percentages.
 *
 * Usage:
 *   ./ex3 <num_accounts> <transactions_per_thread> <read_pct>
 *         <coarse_mutex|fine_mutex|coarse_rw|fine_rw>
 *         <num_threads> <read_work_iters>
 *
 * read_pct        : integer in [0, 100]
 * read_work_iters : number of sqrt iterations inside the read critical section;
 *                   use 1 for minimal work, 100+ to stress rwlock advantage
 *
 * Example:
 *   ./ex3 1000 10000 80 fine_rw 4 100
 */

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SEED        42
#define MAX_AMOUNT  100
#define MAX_THREADS 256

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

static pthread_mutex_t coarse_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_rwlock_t coarse_rw = PTHREAD_RWLOCK_INITIALIZER;
static pthread_mutex_t *fine_mutexes;
static pthread_rwlock_t *fine_rwlocks;

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

/* -------------------------------------------------------------------------
 * Input parsing
 * ---------------------------------------------------------------------- */

/**
 * @brief Parse a string as a positive integer using strtol.
 *
 * @param str   Input string.
 * @param out   Output value on success.
 * @param max   Maximum accepted value (inclusive).
 * @param label Parameter name for error messages.
 * @return 1 on success, 0 on any error.
 */
static int parse_positive_int(const char *str, int *out, int max,
                              const char *label)
{
    if (!str || str[0] == '\0') {
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
 * @param argc            Argument count from main.
 * @param argv            Argument vector from main.
 * @param num_accounts    Output: number of bank accounts.
 * @param transactions    Output: transactions per thread.
 * @param read_pct        Output: percentage of read transactions [0,100].
 * @param scheme          Output: parsed locking scheme enum.
 * @param scheme_str      Output: pointer into argv for the scheme string.
 * @param num_threads     Output: thread count.
 * @param read_work_iters Output: sqrt iterations inside read critical section.
 * @return 1 if all arguments are valid, 0 otherwise.
 */
static int parse_args(int argc, char *argv[],
                      int *num_accounts, int *transactions, int *read_pct,
                      lock_scheme_t *scheme, const char **scheme_str,
                      int *num_threads, int *read_work_iters)
{
    if (argc != 7) {
        fprintf(stderr,
                "Usage: %s <num_accounts> <transactions_per_thread> <read_pct>"
                " <coarse_mutex|fine_mutex|coarse_rw|fine_rw>"
                " <num_threads> <read_work_iters>\n",
                argv[0]);
        return 0;
    }

    if (!parse_positive_int(argv[1], num_accounts, 1000000, "num_accounts")) {
        return 0;
    }

    if (*num_accounts < 2) {
        fprintf(stderr, "Error: num_accounts must be at least 2\n");
        return 0;
    }

    if (!parse_positive_int(argv[2], transactions, 10000000, "transactions")) {
        return 0;
    }

    char *end;
    errno = 0;
    long pct = strtol(argv[3], &end, 10);
    if (errno == ERANGE || end == argv[3] || *end != '\0' ||
        pct < 0 || pct > 100) {
        fprintf(stderr,
                "Error: read_pct must be an integer in [0,100], got '%s'\n",
                argv[3]);
        return 0;
    }
    *read_pct = (int)pct;

    *scheme_str = argv[4];
    if (strcmp(*scheme_str, "coarse_mutex") == 0) {
        *scheme = SCHEME_COARSE_MUTEX;
    } else if (strcmp(*scheme_str, "fine_mutex") == 0) {
        *scheme = SCHEME_FINE_MUTEX;
    } else if (strcmp(*scheme_str, "coarse_rw") == 0) {
        *scheme = SCHEME_COARSE_RW;
    } else if (strcmp(*scheme_str, "fine_rw") == 0) {
        *scheme = SCHEME_FINE_RW;
    } else {
        fprintf(stderr,
                "Error: scheme must be coarse_mutex, fine_mutex, coarse_rw, "
                "or fine_rw; got '%s'\n", *scheme_str);
        return 0;
    }

    if (!parse_positive_int(argv[5], num_threads, MAX_THREADS, "num_threads")) {
        return 0;
    }

    if (!parse_positive_int(argv[6], read_work_iters, 100000,
                            "read_work_iters")) {
        return 0;
    }

    return 1;
}

/* -------------------------------------------------------------------------
 * Read work helper
 * ---------------------------------------------------------------------- */

/**
 * @brief Simulate computational work inside the read critical section.
 *
 * Performs read_work_iters iterations of sqrt to model a non-trivial read
 * operation. Increasing this value makes the read critical section longer,
 * which amplifies the advantage of rwlocks over mutexes at high read ratios.
 *
 * @param balance         Account balance read from the array.
 * @param read_work_iters Number of sqrt iterations to perform.
 * @param accum           Private accumulator updated with the result.
 */
static void read_work(int balance, int read_work_iters, long long *accum)
{
    double tmp = 0.0;
    for (int i = 0; i < read_work_iters; i++) {
        tmp += sqrt((double)abs(balance) + 1.0 + i);
    }
    *accum += (long long)tmp;
}

/* -------------------------------------------------------------------------
 * Transaction implementations
 * ---------------------------------------------------------------------- */

static void transfer_coarse_mutex(int src, int dst, int amount)
{
    pthread_mutex_lock(&coarse_mutex);
    accounts[src] -= amount;
    accounts[dst] += amount;
    pthread_mutex_unlock(&coarse_mutex);
}

static void query_coarse_mutex(int idx, int read_work_iters, long long *accum)
{
    pthread_mutex_lock(&coarse_mutex);
    int balance = accounts[idx];
    read_work(balance, read_work_iters, accum);
    pthread_mutex_unlock(&coarse_mutex);
}

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

static void query_fine_mutex(int idx, int read_work_iters, long long *accum)
{
    pthread_mutex_lock(&fine_mutexes[idx]);
    int balance = accounts[idx];
    read_work(balance, read_work_iters, accum);
    pthread_mutex_unlock(&fine_mutexes[idx]);
}

static void transfer_coarse_rw(int src, int dst, int amount)
{
    pthread_rwlock_wrlock(&coarse_rw);
    accounts[src] -= amount;
    accounts[dst] += amount;
    pthread_rwlock_unlock(&coarse_rw);
}

static void query_coarse_rw(int idx, int read_work_iters, long long *accum)
{
    pthread_rwlock_rdlock(&coarse_rw);
    int balance = accounts[idx];
    read_work(balance, read_work_iters, accum);
    pthread_rwlock_unlock(&coarse_rw);
}

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

static void query_fine_rw(int idx, int read_work_iters, long long *accum)
{
    pthread_rwlock_rdlock(&fine_rwlocks[idx]);
    int balance = accounts[idx];
    read_work(balance, read_work_iters, accum);
    pthread_rwlock_unlock(&fine_rwlocks[idx]);
}

/* -------------------------------------------------------------------------
 * Thread arguments and worker
 * ---------------------------------------------------------------------- */

/**
 * @brief Arguments passed to each bank simulation thread.
 */
typedef struct {
    int transactions;     /**< Total transactions to perform. */
    int read_pct;         /**< Percentage [0,100] of read transactions. */
    int read_work_iters;  /**< sqrt iterations inside read critical section. */
    lock_scheme_t scheme; /**< Locking scheme to use. */
    unsigned int seed;    /**< Per-thread RNG seed. */
    long long local_sum;  /**< Private read-query checksum for this thread. */
} bank_args_t;

/**
 * @brief Thread worker: perform the assigned number of bank transactions.
 *
 * Randomly selects transaction type based on read_pct. The balance query
 * accumulator is kept private to avoid shared writes outside the lock.
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
            case SCHEME_COARSE_MUTEX:
                query_coarse_mutex(idx, args->read_work_iters, &local_sum);
                break;
            case SCHEME_FINE_MUTEX:
                query_fine_mutex(idx, args->read_work_iters, &local_sum);
                break;
            case SCHEME_COARSE_RW:
                query_coarse_rw(idx, args->read_work_iters, &local_sum);
                break;
            case SCHEME_FINE_RW:
                query_fine_rw(idx, args->read_work_iters, &local_sum);
                break;
            }
        } else {
            int src = rand_r(&seed) % num_accounts;
            int dst;
            do {
                dst = rand_r(&seed) % num_accounts;
            } while (dst == src);

            int amount = (rand_r(&seed) % MAX_AMOUNT) + 1;
            switch (args->scheme) {
            case SCHEME_COARSE_MUTEX:
                transfer_coarse_mutex(src, dst, amount);
                break;
            case SCHEME_FINE_MUTEX:
                transfer_fine_mutex(src, dst, amount);
                break;
            case SCHEME_COARSE_RW:
                transfer_coarse_rw(src, dst, amount);
                break;
            case SCHEME_FINE_RW:
                transfer_fine_rw(src, dst, amount);
                break;
            }
        }
    }

    args->local_sum = local_sum;
    return NULL;
}

/* -------------------------------------------------------------------------
 * Verification
 * ---------------------------------------------------------------------- */

/**
 * @brief Verify that the total sum of all account balances is unchanged.
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
 *             num_threads, read_work_iters.
 * @return 0 on success, 1 on argument error, 2 on correctness failure.
 */
int main(int argc, char *argv[])
{
    int num_accs, transactions, read_pct, num_threads, read_work_iters;
    lock_scheme_t scheme;
    const char *scheme_str;

    if (!parse_args(argc, argv, &num_accs, &transactions, &read_pct,
                    &scheme, &scheme_str, &num_threads, &read_work_iters)) {
        return 1;
    }

    num_accounts = num_accs;

    fprintf(stderr,
            "[ex3] scheme=%s threads=%d accounts=%d "
            "transactions=%d read_pct=%d%% read_work_iters=%d\n",
            scheme_str, num_threads, num_accounts,
            transactions, read_pct, read_work_iters);
    fprintf(stderr, "[ex3] initialising accounts...\n");

    srand(SEED);
    accounts = xmalloc((size_t)num_accounts, sizeof(int));
    initial_total = 0;
    for (int i = 0; i < num_accounts; i++) {
        accounts[i] = (rand() % 10000) + 1;
        initial_total += accounts[i];
    }

    if (scheme == SCHEME_FINE_MUTEX) {
        fine_mutexes = xmalloc((size_t)num_accounts, sizeof(pthread_mutex_t));
        for (int i = 0; i < num_accounts; i++) {
            pthread_mutex_init(&fine_mutexes[i], NULL);
        }
    }

    if (scheme == SCHEME_FINE_RW) {
        fine_rwlocks = xmalloc((size_t)num_accounts, sizeof(pthread_rwlock_t));
        for (int i = 0; i < num_accounts; i++) {
            pthread_rwlock_init(&fine_rwlocks[i], NULL);
        }
    }

    pthread_t *threads = xmalloc((size_t)num_threads, sizeof(pthread_t));
    bank_args_t *args = xmalloc((size_t)num_threads, sizeof(bank_args_t));

    fprintf(stderr, "[ex3] spawning threads...\n");

    double t0 = now();
    for (int t = 0; t < num_threads; t++) {
        args[t].transactions = transactions;
        args[t].read_pct = read_pct;
        args[t].read_work_iters = read_work_iters;
        args[t].scheme = scheme;
        args[t].seed = (unsigned int)(SEED + t);
        args[t].local_sum = 0;

        int rc = pthread_create(&threads[t], NULL, bank_worker, &args[t]);
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

    long long read_checksum = 0;
    for (int t = 0; t < num_threads; t++) {
        read_checksum += args[t].local_sum;
    }

    double elapsed = now() - t0;

    fprintf(stderr, "[ex3] done (%.6f s)\n", elapsed);

    printf("Scheme:          %s\n", scheme_str);
    printf("Threads:         %d\n", num_threads);
    printf("Accounts:        %d\n", num_accounts);
    printf("Transactions:    %d per thread\n", transactions);
    printf("Read pct:        %d%%\n", read_pct);
    printf("Read work iters: %d\n", read_work_iters);
    printf("Read checksum:   %lld\n", read_checksum);
    printf("Elapsed:         %.6f s\n", elapsed);

    int ok = verify_total();
    printf("Correctness:     %s\n", ok ? "[OK]" : "[FAIL]");

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

    return ok ? 0 : 2;
}
