/* smtx.h - v1.0 - Shared Mutex (Reader-Writer Lock) Implementation
                          no warranty implied; use at your own risk

   This is a single-header library for a high-performance, lock-free shared mutex
   (reader-writer lock) implementation using C11 atomics. It enables multiple readers
   to concurrently access a shared resource while ensuring exclusive access for writers.

   Key features:
     - Lock-free implementation using C11 atomics
     - Spin-then-yield strategy with exponential backoff
     - Architecture-specific optimizations (x86/x64 pause instruction)
     - Comprehensive API including timed lock operations
     - Thorough debugging support with runtime invariant checks

   Usage:
     #define SMTX_IMPLEMENTATION
     #include "smtx.h"

   Options:
     #define SMTX_STATIC                 - if static functions are preferred
     #define SMTX_NDEBUG                 - disable debug assertions and checks
     #define SMTX_ASSERT(expr)           - override default assert implementation (default: assert)
     #define SMTX_NEXT_SPINS(curr_spins) - override spin count progression strategy, initial curr_spins is always 1 (default: exponential backoff via curr_spins * 2)
     #define SMTX_MAX_WRITER_WAIT_SPINS  - maximum spin count when waiting for writers (default: 1024)
     #define SMTX_MAX_READER_WAIT_SPINS  - maximum spin count when waiting for readers (default: 1024)
     #define SMTX_YIELD_THRESHOLD        - spin count threshold before yielding the thread (default: 512)
     #define SMTX_YIELD                  - override thread yielding mechanism (default: thrd_yield() from <threads.h>)
     #define SMTX_CLOCK_ID               - clock ID to use for timeouts (default: CLOCK_MONOTONIC)
     #define SMTX_CACHE_LINE_SIZE        - cache line size in bytes (default: 64)
     #define SMTX_PREVENT_FALSE_SHARING  - add padding and enforce alignment of SMTX_CACHE_LINE_SIZE

   License: MIT (see end of file for license information)
*/

#ifndef SMTX_H
#define SMTX_H

#ifdef __STDC_NO_ATOMICS__
#error "smtx library requires atomics support which is not provided on current machine, currently no fallback is provided."
#endif

#include <stdatomic.h>
#include <time.h>

#undef SMTX_DEF
#ifdef SMTX_STATIC
    #define SMTX_DEF static
#else
    #define SMTX_DEF extern
#endif

#ifndef SMTX_CACHE_LINE_SIZE
#define SMTX_CACHE_LINE_SIZE 64
#endif

#ifdef SMTX_PREVENT_FALSE_SHARING
#include <stdalign.h>
typedef struct {
    alignas(SMTX_CACHE_LINE_SIZE) union {
        atomic_uint reader_count;
        char _pad0[SMTX_CACHE_LINE_SIZE];
    };

    alignas(SMTX_CACHE_LINE_SIZE) union {
        atomic_bool writer_locked;
        char _pad1[SMTX_CACHE_LINE_SIZE];
    };
} smtx_t;
#else
typedef struct {
    atomic_uint reader_count;
    atomic_bool writer_locked;
} smtx_t;
#endif


SMTX_DEF int smtx_init(smtx_t *smtx);

SMTX_DEF int smtx_lock_shared     (smtx_t *smtx);
SMTX_DEF int smtx_trylock_shared  (smtx_t *smtx);
SMTX_DEF int smtx_timedlock_shared(smtx_t *smtx, const struct timespec *time_point);
SMTX_DEF int smtx_unlock_shared   (smtx_t *smtx);

SMTX_DEF int smtx_lock_exclusive     (smtx_t *smtx);
SMTX_DEF int smtx_trylock_exclusive  (smtx_t *smtx);
SMTX_DEF int smtx_timedlock_exclusive(smtx_t *smtx, const struct timespec *time_point);
SMTX_DEF int smtx_unlock_exclusive   (smtx_t *smtx);

#endif //SMTX_H

#ifdef SMTX_IMPLEMENTATION

#include <stdbool.h>
#include <stdint.h>

#undef SMTX_UTIL
#define SMTX_UTIL static inline

#undef SMTX_IMPL
#ifdef SMTX_STATIC
#define SMTX_IMPL static inline
#else
#define SMTX_IMPL extern inline
#endif

typedef uint64_t smtx_ns_t;

#ifndef SMTX_NDEBUG
#define SMTX_DEBUG
#endif

#ifndef SMTX_ASSERT
#include <assert.h>
#define SMTX_ASSERT(expr) assert(expr)
#endif

#ifndef SMTX_NEXT_SPINS
#define SMTX_NEXT_SPINS(curr_spins) ((curr_spins) * 2)
#endif

#ifndef SMTX_MAX_WRITER_WAIT_SPINS
#define SMTX_MAX_WRITER_WAIT_SPINS 1024
#endif

#ifndef SMTX_MAX_READER_WAIT_SPINS
#define SMTX_MAX_READER_WAIT_SPINS 1024
#endif

#ifndef SMTX_YIELD_THRESHOLD
#define SMTX_YIELD_THRESHOLD 512
#endif

#ifndef SMTX_YIELD
#include <threads.h>
#define SMTX_YIELD thrd_yield()
#endif

#ifndef SMTX_CLOCK_ID
#define SMTX_CLOCK_ID CLOCK_MONOTONIC
#endif

#undef SPIN
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#define SPIN(delay)                         \
    do {                                    \
        for (int i = 0; i < (delay); ++i) { \
            _mm_pause();                    \
        }                                   \
    } while (0)
#else
#define SPIN(delay)                                  \
    do {                                             \
        for (volatile int i = 0; i < (delay); ++i) { \
        }                                            \
    } while (0)
#endif

#undef SMTX_NS_PER_S
#define SMTX_NS_PER_S 1000000000LL

SMTX_UTIL smtx_ns_t ns_from_timespec(const struct timespec *ts) {
    return (smtx_ns_t)ts->tv_sec * SMTX_NS_PER_S + ts->tv_nsec;
}

SMTX_UTIL smtx_ns_t ns_since_epoch() {
    struct timespec ts;
    SMTX_ASSERT(clock_gettime(SMTX_CLOCK_ID, &ts) == 0);
    return ns_from_timespec(&ts);
}

SMTX_UTIL void spin_with_yield(uint spins) {
    SPIN(spins);
    if (spins > SMTX_YIELD_THRESHOLD) {
        SMTX_YIELD;
    }
}

SMTX_IMPL int smtx_init(smtx_t *smtx) {
    if (smtx == NULL) {
        return thrd_error;
    }

    atomic_init(&smtx->reader_count, 0);
    atomic_init(&smtx->writer_locked, false);

    return thrd_success;
}

SMTX_IMPL int smtx_lock_shared(smtx_t *smtx) {
    if (smtx == NULL) {
        return thrd_error;
    }

    uint spins = 1;
    while (true) {
        while (atomic_load_explicit(&smtx->writer_locked, memory_order_acquire)) {
            spin_with_yield(spins);
            if (spins < SMTX_MAX_WRITER_WAIT_SPINS) {
                spins = SMTX_NEXT_SPINS(spins);
            }
        }

        atomic_fetch_add_explicit(&smtx->reader_count, 1, memory_order_relaxed);

        if (!atomic_load_explicit(&smtx->writer_locked, memory_order_acquire)) {
            return thrd_success;
        }

        atomic_fetch_sub_explicit(&smtx->reader_count, 1, memory_order_release);
    }
}

SMTX_IMPL int smtx_trylock_shared(smtx_t *smtx) {
    if (smtx == NULL) {
        return thrd_error;
    }

    if (atomic_load_explicit(&smtx->writer_locked, memory_order_acquire)) {
        return thrd_busy;
    }

    atomic_fetch_add_explicit(&smtx->reader_count, 1, memory_order_relaxed);

    if (atomic_load_explicit(&smtx->writer_locked, memory_order_acquire)) {
        atomic_fetch_sub_explicit(&smtx->reader_count, 1, memory_order_release);
        return thrd_busy;
    }

    return thrd_success;
}

SMTX_IMPL int smtx_timedlock_shared(smtx_t *smtx, const struct timespec *time_point) {
    if (smtx == NULL || time_point == NULL) {
        return thrd_error;
    }

    uint spins = 1;
    const smtx_ns_t deadline = ns_from_timespec(time_point);
    while (ns_since_epoch() < deadline) {
        if (atomic_load_explicit(&smtx->writer_locked, memory_order_acquire)) {
            spin_with_yield(spins);
            if (spins < SMTX_MAX_WRITER_WAIT_SPINS) {
                spins = SMTX_NEXT_SPINS(spins);
            }
            continue;
        }

        atomic_fetch_add_explicit(&smtx->reader_count, 1, memory_order_relaxed);

        if (!atomic_load_explicit(&smtx->writer_locked, memory_order_acquire)) {
            return thrd_success;
        }

        atomic_fetch_sub_explicit(&smtx->reader_count, 1, memory_order_release);

        spin_with_yield(spins);
        if (spins < SMTX_MAX_WRITER_WAIT_SPINS) {
            spins = SMTX_NEXT_SPINS(spins);
        }
    }

    return thrd_timedout;
}

SMTX_IMPL int smtx_unlock_shared(smtx_t *smtx) {
    if (smtx == NULL) {
        return thrd_error;
    }

#ifdef SMTX_DEBUG
    SMTX_ASSERT(atomic_load_explicit(&smtx->reader_count, memory_order_relaxed) > 0);
#endif

    atomic_fetch_sub_explicit(&smtx->reader_count, 1, memory_order_release);

    return thrd_success;
}

SMTX_IMPL int smtx_lock_exclusive(smtx_t *smtx) {
    if (smtx == NULL) {
        return thrd_error;
    }

    bool expected = false;
    while (!atomic_compare_exchange_weak_explicit(&smtx->writer_locked, &expected, true, memory_order_acquire, memory_order_relaxed)) {
        expected = false;
    }

    uint spins = 1;
    while (atomic_load_explicit(&smtx->reader_count, memory_order_acquire) > 0) {
        spin_with_yield(spins);
        if (spins < SMTX_MAX_READER_WAIT_SPINS) {
            spins = SMTX_NEXT_SPINS(spins);
        }
    }

    return thrd_success;
}

SMTX_IMPL int smtx_trylock_exclusive(smtx_t *smtx) {
    if (smtx == NULL) {
        return thrd_error;
    }

    bool expected = false;
    if (!atomic_compare_exchange_weak_explicit(&smtx->writer_locked, &expected, true, memory_order_acquire, memory_order_relaxed)) {
        return thrd_busy;
    }

    if (atomic_load_explicit(&smtx->reader_count, memory_order_acquire) > 0) {
        atomic_store_explicit(&smtx->writer_locked, false, memory_order_release);
        return thrd_busy;
    }

    return thrd_success;
}

SMTX_IMPL int smtx_timedlock_exclusive(smtx_t *smtx, const struct timespec *time_point) {
    if (smtx == NULL || time_point == NULL) {
        return thrd_error;
    }

    uint spins = 1;
    const smtx_ns_t deadline = ns_from_timespec(time_point);
    bool expected = false;

    while (!atomic_compare_exchange_weak_explicit(&smtx->writer_locked, &expected, true, memory_order_acquire, memory_order_relaxed)) {
        if (ns_since_epoch() >= deadline) {
            return thrd_timedout;
        }
        expected = false;
        spin_with_yield(spins);
        if (spins < SMTX_MAX_READER_WAIT_SPINS) {
            spins = SMTX_NEXT_SPINS(spins);
        }
    }

    while (atomic_load_explicit(&smtx->reader_count, memory_order_acquire) > 0) {
        if (ns_since_epoch() >= deadline) {
            atomic_store_explicit(&smtx->writer_locked, false, memory_order_release);
            return thrd_timedout;
        }
        spin_with_yield(spins);
        if (spins < SMTX_MAX_READER_WAIT_SPINS) {
            spins = SMTX_NEXT_SPINS(spins);
        }
    }

    return thrd_success;
}

SMTX_IMPL int smtx_unlock_exclusive(smtx_t *smtx) {
    if (smtx == NULL) {
        return thrd_error;
    }

#ifdef SMTX_DEBUG
    SMTX_ASSERT(atomic_load_explicit(&smtx->writer_locked, memory_order_relaxed));
#endif

    atomic_store_explicit(&smtx->writer_locked, false, memory_order_release);

    return thrd_success;
}

#endif // SMTX_IMPLEMENTATION

/*
   Copyright 2025 Karlo Bratko <kbratko@tuta.io>

   Permission is hereby granted, free of charge, to any person obtaining a copy of this software
   and associated documentation files (the “Software”), to deal in the Software without
   restriction, including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all copies or
   substantial portions of the Software.

   THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
   OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.
*/