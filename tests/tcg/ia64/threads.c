/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Threads through clone2: each thread has its own memory stack, register
 * backing store and thread pointer (r13), and they share memory through
 * atomics, a mutex and a condition variable.
 */

#include <pthread.h>
#include <string.h>
#include "ia64-test.h"

#define THREADS 8
#define ROUNDS 20000

static __thread uint64_t tls_value = 7;
static __thread char tls_buffer[64];
static uint64_t atomic_counter;
static uint64_t locked_counter;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static int ready;

static __attribute__((noinline)) uint64_t recurse(uint64_t n, uint64_t salt)
{
    uint64_t a = n * salt, b = a ^ (n << 3), c = b + salt;

    if (n == 0) {
        return salt;
    }
    return recurse(n - 1, salt) + a + b - c;
}

static uint64_t recurse_reference(uint64_t n, uint64_t salt)
{
    uint64_t total = salt;

    for (uint64_t i = 1; i <= n; i++) {
        uint64_t a = i * salt, b = a ^ (i << 3), c = b + salt;

        total += a + b - c;
    }
    return total;
}

static void *worker(void *arg)
{
    uintptr_t id = (uintptr_t)arg;
    uint64_t ok = 1;

    CHECK_EQ(tls_value, 7);
    tls_value = 100 + id;
    snprintf(tls_buffer, sizeof(tls_buffer), "thread %u", (unsigned)id);

    pthread_mutex_lock(&lock);
    while (!ready) {
        pthread_cond_wait(&cond, &lock);
    }
    pthread_mutex_unlock(&lock);

    for (int i = 0; i < ROUNDS; i++) {
        __sync_fetch_and_add(&atomic_counter, 1);
        pthread_mutex_lock(&lock);
        locked_counter += 2;
        pthread_mutex_unlock(&lock);
    }
    /* Deep enough to spill this thread's register stack many times. */
    if (recurse(3000 + id, id + 1) != recurse_reference(3000 + id, id + 1)) {
        ok = 0;
    }
    if (tls_value != 100 + id) {
        ok = 0;
    }
    {
        char want[64];

        snprintf(want, sizeof(want), "thread %u", (unsigned)id);
        if (strcmp(tls_buffer, want) != 0) {
            ok = 0;
        }
    }
    return (void *)(ok * (id + 1000));
}

int main(void)
{
    pthread_t threads[THREADS];

    for (uintptr_t i = 0; i < THREADS; i++) {
        CHECK_EQ(pthread_create(&threads[i], NULL, worker, (void *)i), 0);
    }
    pthread_mutex_lock(&lock);
    ready = 1;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);

    for (uintptr_t i = 0; i < THREADS; i++) {
        void *ret;

        CHECK_EQ(pthread_join(threads[i], &ret), 0);
        CHECK_EQ((uintptr_t)ret, i + 1000);
    }
    CHECK_EQ(atomic_counter, (uint64_t)THREADS * ROUNDS);
    CHECK_EQ(locked_counter, (uint64_t)THREADS * ROUNDS * 2);
    CHECK_EQ(tls_value, 7);
    return test_finish("threads");
}
