#include <assert.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <unistd.h>

#include "../smtx.h"

#define NUM_THREADS 32
#define TEST_DURATION_SECONDS 10
#define WRITER_RATIO 0.25 // 25% of threads are writers

#define NS_PER_MS 1000000

smtx_t smtx;
int global_value = 0;
int write_count = 0;
int read_count = 0;
atomic_bool stop_flag = false;

static void random_delay(unsigned *seed, int max_ns) {
    thrd_sleep(&(struct timespec){.tv_sec = 0, .tv_nsec = rand_r(seed) % max_ns}, NULL);
}

static int stress_worker(void* arg) {
    const uintptr_t tid = (uintptr_t)arg;
    unsigned seed = tid * 7919 + 17; // deterministic per-thread
    const int is_writer = ((rand_r(&seed) % 100) < (WRITER_RATIO * 100));

    while (!atomic_load_explicit(&stop_flag, memory_order_relaxed)) {
        if (is_writer) {
            smtx_lock_exclusive(&smtx);

            global_value += 1;
            write_count += 1;

            printf("    [Writer %3" PRIuPTR "] Wrote value = %d\n", tid, global_value);

            smtx_unlock_exclusive(&smtx);
        } else {
            smtx_lock_shared(&smtx);

            read_count += 1;

            printf("[Reader %3" PRIuPTR "] Read value = %d\n", tid, global_value);

            smtx_unlock_shared(&smtx);
        }

        random_delay(&seed, 1 * NS_PER_MS); // up to 1ms
    }

    return 0;
}

int main(void) {
    printf("[TEST] Starting smtx stress test with %d threads for %d seconds...\n",
           NUM_THREADS, TEST_DURATION_SECONDS);

    smtx_init(&smtx);
    atomic_init(&stop_flag, false);

    thrd_t threads[NUM_THREADS];

    for (uintptr_t i = 0; i < NUM_THREADS; ++i) {
        assert(thrd_create(&threads[i], stress_worker, (void*)i) == thrd_success);
    }

    sleep(TEST_DURATION_SECONDS);
    atomic_store(&stop_flag, true);

    for (int i = 0; i < NUM_THREADS; ++i) {
        assert(thrd_join(threads[i], NULL) == thrd_success);
    }


    printf("\n[TEST] Final global value = %d\n", global_value);
    printf("[TEST] Total write count  = %d\n", write_count);
    printf("[TEST] Total read count  = %d\n", read_count);

    return EXIT_SUCCESS;
}
