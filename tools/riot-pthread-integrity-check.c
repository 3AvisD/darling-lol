#include <dlfcn.h>
#include <errno.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static unsigned counter, turn, cancel_ready;

static void *increment(void *unused) {
    (void)unused;
    for (unsigned i = 0; i < 10000; ++i) {
        pthread_mutex_lock(&mutex);
        ++counter;
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

static void *handoff(void *argument) {
    unsigned side = (unsigned)(uintptr_t)argument;
    for (unsigned i = 0; i < 1000; ++i) {
        pthread_mutex_lock(&mutex);
        while (turn != side) pthread_cond_wait(&condition, &mutex);
        turn = 1 - side;
        pthread_cond_signal(&condition);
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

static void unlock(void *value) { pthread_mutex_unlock(value); }
static void *cancel_wait(void *unused) {
    (void)unused;
    pthread_mutex_lock(&mutex);
    pthread_cleanup_push(unlock, &mutex);
    cancel_ready = 1;
    pthread_cond_signal(&condition);
    for (;;) {
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_nsec += 20000000;
        if (deadline.tv_nsec >= 1000000000) { ++deadline.tv_sec; deadline.tv_nsec -= 1000000000; }
        pthread_cond_timedwait(&condition, &mutex, &deadline);
    }
    pthread_cleanup_pop(1);
    return NULL;
}

int main(void) {
    Dl_info info = {0};
    int isolated = dladdr((void *)pthread_mutex_lock, &info) && info.dli_fname &&
                   strstr(info.dli_fname, "riot-pthread-check-runtime") != NULL;
    pthread_t threads[4];
    for (unsigned i = 0; i < 4; ++i)
        if (pthread_create(&threads[i], NULL, increment, NULL)) return 1;
    for (unsigned i = 0; i < 4; ++i) pthread_join(threads[i], NULL);
    if (counter != 40000) return 2;
    for (unsigned i = 0; i < 2; ++i)
        if (pthread_create(&threads[i], NULL, handoff, (void *)(uintptr_t)i)) return 3;
    for (unsigned i = 0; i < 2; ++i) pthread_join(threads[i], NULL);
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += 20000000;
    if (deadline.tv_nsec >= 1000000000) { ++deadline.tv_sec; deadline.tv_nsec -= 1000000000; }
    pthread_mutex_lock(&mutex);
    int timeout = pthread_cond_timedwait(&condition, &mutex, &deadline);
    pthread_mutex_unlock(&mutex);
    if (timeout != ETIMEDOUT) return 4;
    if (pthread_create(&threads[0], NULL, cancel_wait, NULL)) return 5;
    pthread_mutex_lock(&mutex);
    while (!cancel_ready) pthread_cond_wait(&condition, &mutex);
    pthread_mutex_unlock(&mutex);
    pthread_cancel(threads[0]);
    void *result = NULL;
    pthread_join(threads[0], &result);
    if (result != PTHREAD_CANCELED || pthread_mutex_trylock(&mutex)) return 6;
    pthread_mutex_unlock(&mutex);
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    uint64_t start = mach_absolute_time();
    for (unsigned i = 0; i < 100000; ++i) {
        pthread_mutex_lock(&mutex);
        __asm__ volatile("" : : : "memory");
        pthread_mutex_unlock(&mutex);
    }
    uint64_t nanoseconds = (mach_absolute_time() - start) * timebase.numer / timebase.denom;
    printf("PTHREAD_INTEGRITY isolated_candidate=%d contended_increments=%u handoffs=2000 timeout=1 timed_wait_cancellation=1 ns_per_pair=%llu pass=1\n",
           isolated, counter, (unsigned long long)(nanoseconds / 100000));
    return 0;
}
