/* Check of process-local Mach semaphores (libsystem_kernel mach_traps.c): counting under contention, timeouts,
 * destroy with waiters, signal_all, dispatch semaphores, and the cost of a thread-to-thread hand-off. */
#include <mach/mach.h>
#include <mach/semaphore.h>
#include <mach/task.h>
#include <mach/mach_time.h>
#include <dispatch/dispatch.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

static semaphore_t items, ping, pong, doomed, all;
static long consumed;
static int woke_all;
static kern_return_t doomed_result = -1;

static void* consumer(void* arg) { for (int i = 0; i < 20000; ++i) { if (semaphore_wait(items) != KERN_SUCCESS) return (void*)1; __atomic_add_fetch(&consumed, 1, __ATOMIC_RELAXED); } return 0; }
static void* producer(void* arg) { for (int i = 0; i < 20000; ++i) if (semaphore_signal(items) != KERN_SUCCESS) return (void*)1; return 0; }
static void* ponger(void* arg) { for (int i = 0; i < 20000; ++i) { semaphore_wait(ping); semaphore_signal(pong); } return 0; }
static void* doomed_waiter(void* arg) { doomed_result = semaphore_wait(doomed); return 0; }
static void* all_waiter(void* arg) { if (semaphore_wait(all) == KERN_SUCCESS) __atomic_add_fetch(&woke_all, 1, __ATOMIC_RELAXED); return 0; }

int main(void) {
    int bad = 0;
    task_t self = mach_task_self();
    if (semaphore_create(self, &items, SYNC_POLICY_FIFO, 0) || semaphore_create(self, &ping, SYNC_POLICY_FIFO, 0) ||
        semaphore_create(self, &pong, SYNC_POLICY_FIFO, 0) || semaphore_create(self, &doomed, SYNC_POLICY_FIFO, 0) ||
        semaphore_create(self, &all, SYNC_POLICY_FIFO, 0)) return 1;

    pthread_t t[8];
    for (int i = 0; i < 4; ++i) pthread_create(&t[i], 0, consumer, 0);
    for (int i = 4; i < 8; ++i) pthread_create(&t[i], 0, producer, 0);
    for (int i = 0; i < 8; ++i) { void* r; pthread_join(t[i], &r); if (r) bad |= 1; }
    printf("COUNT: 4 producers x 20000, consumed %ld (expected 80000)\n", consumed);
    if (consumed != 80000) bad |= 2;
    mach_timespec_t zero = {0, 1000000};
    kern_return_t kr = semaphore_timedwait(items, zero);
    printf("LEFTOVER: timedwait on the drained semaphore -> %d (expected %d)\n", kr, KERN_OPERATION_TIMED_OUT);
    if (kr != KERN_OPERATION_TIMED_OUT) bad |= 4;

    uint64_t a = mach_absolute_time();
    mach_timespec_t wait = {0, 50000000};
    kr = semaphore_timedwait(items, wait);
    uint64_t b = mach_absolute_time();
    printf("TIMEOUT: 50 ms timedwait -> %d after %.1f ms\n", kr, (b - a) / 1e6);
    if (kr != KERN_OPERATION_TIMED_OUT || b - a < 49000000ull || b - a > 80000000ull) bad |= 8;
    semaphore_signal(items);
    mach_timespec_t longwait = {5, 0};
    kr = semaphore_timedwait(items, longwait);
    printf("TIMED SUCCESS: %d\n", kr);
    if (kr != KERN_SUCCESS) bad |= 16;

    pthread_t p;
    pthread_create(&p, 0, ponger, 0);
    a = mach_absolute_time();
    for (int i = 0; i < 20000; ++i) { semaphore_signal(ping); semaphore_wait(pong); }
    b = mach_absolute_time();
    pthread_join(p, 0);
    printf("HANDOFF: signal + wait round trip between two threads %.1f us\n", (b - a) / 20000.0 / 1000.0);
    a = mach_absolute_time();
    for (int i = 0; i < 200000; ++i) { semaphore_signal(ping); semaphore_wait(ping); }
    b = mach_absolute_time();
    printf("UNCONTENDED: signal + wait on one thread %.0f ns\n", (b - a) / 200000.0);

    pthread_t d;
    pthread_create(&d, 0, doomed_waiter, 0);
    usleep(100000);
    semaphore_destroy(self, doomed);
    pthread_join(d, 0);
    printf("DESTROY: waiter returned %d (expected %d)\n", doomed_result, KERN_TERMINATED);
    if (doomed_result != KERN_TERMINATED) bad |= 32;

    pthread_t w[3];
    for (int i = 0; i < 3; ++i) pthread_create(&w[i], 0, all_waiter, 0);
    usleep(100000);
    semaphore_signal_all(all);
    for (int i = 0; i < 3; ++i) pthread_join(w[i], 0);
    kr = semaphore_timedwait(all, zero);
    printf("SIGNAL_ALL: woke %d of 3, nothing left over: %d\n", woke_all, kr == KERN_OPERATION_TIMED_OUT);
    if (woke_all != 3 || kr != KERN_OPERATION_TIMED_OUT) bad |= 64;

    dispatch_semaphore_t ds = dispatch_semaphore_create(0);
    __block int ran = 0;
    for (int i = 0; i < 1000; ++i) dispatch_async(dispatch_get_global_queue(0, 0), ^{ __atomic_add_fetch(&ran, 1, __ATOMIC_RELAXED); dispatch_semaphore_signal(ds); });
    for (int i = 0; i < 1000; ++i) if (dispatch_semaphore_wait(ds, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC))) { bad |= 128; break; }
    printf("DISPATCH: 1000 blocks signalled a dispatch semaphore, ran %d\n", ran);
    printf("RESULT: %s (%d)\n", bad ? "FAILED" : "ok", bad);
    return bad;
}
