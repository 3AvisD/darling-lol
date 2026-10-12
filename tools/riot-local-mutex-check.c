/* Check of in-process first-fit pthread mutexes (libsystem_kernel psynch_mutexwait.c): mutual exclusion under heavy
 * contention, many mutexes, recursive and error-checking types, condition variables whose mutex has waiters, timed
 * waits, a fair-share mutex (stays on the server) next to them, fork, and the cost of a contended hand-off. */
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <mach/mach_time.h>

#define THREADS 8
static pthread_mutex_t big = PTHREAD_MUTEX_INITIALIZER, many[64], recursive, fair;
static volatile long inside, violations, counter, many_counter[64], rec_counter, fair_counter;
static pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t qcond = PTHREAD_COND_INITIALIZER, qspace = PTHREAD_COND_INITIALIZER;
static int queue_len; static long produced, consumed; static int done_producing;

static void* hammer(void* arg) {
    unsigned seed = (unsigned)(uintptr_t)arg * 2654435761u;
    for (int i = 0; i < 60000; ++i) {
        pthread_mutex_lock(&big);
        if (__atomic_add_fetch(&inside, 1, __ATOMIC_SEQ_CST) != 1) __atomic_add_fetch(&violations, 1, __ATOMIC_SEQ_CST);
        counter++;
        if ((i & 255) == 0) sched_yield();
        __atomic_sub_fetch(&inside, 1, __ATOMIC_SEQ_CST);
        pthread_mutex_unlock(&big);
        seed = seed * 1103515245u + 12345u;
        int k = (seed >> 16) & 63;
        pthread_mutex_lock(&many[k]); many_counter[k]++; pthread_mutex_unlock(&many[k]);
        if ((i & 7) == 0) {
            pthread_mutex_lock(&recursive); pthread_mutex_lock(&recursive); rec_counter++; pthread_mutex_unlock(&recursive); pthread_mutex_unlock(&recursive);
            pthread_mutex_lock(&fair); fair_counter++; pthread_mutex_unlock(&fair);
        }
    }
    return 0;
}
static void* producer(void* arg) {
    for (int i = 0; i < 20000; ++i) {
        pthread_mutex_lock(&qlock);
        while (queue_len >= 8) pthread_cond_wait(&qspace, &qlock);
        queue_len++; produced++;
        pthread_cond_signal(&qcond);
        pthread_mutex_unlock(&qlock);
    }
    return 0;
}
static void* consumer(void* arg) {
    for (;;) {
        pthread_mutex_lock(&qlock);
        while (queue_len == 0 && !done_producing) {
            struct timeval tv; gettimeofday(&tv, 0);
            struct timespec ts = { tv.tv_sec, (tv.tv_usec + 20000) * 1000 };
            if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
            pthread_cond_timedwait(&qcond, &qlock, &ts);
        }
        if (queue_len == 0 && done_producing) { pthread_mutex_unlock(&qlock); return 0; }
        queue_len--; consumed++;
        pthread_cond_signal(&qspace);
        pthread_mutex_unlock(&qlock);
    }
}
static pthread_mutex_t ping_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int ping_turn, ping_stop;
static void* pinger(void* arg) {
    int me = (int)(uintptr_t)arg;
    while (!ping_stop) { pthread_mutex_lock(&ping_lock); if (ping_turn == me) ping_turn = !me; pthread_mutex_unlock(&ping_lock); }
    return 0;
}
int main(void) {
    int bad = 0; pthread_t t[THREADS + 8];
    for (int i = 0; i < 64; ++i) pthread_mutex_init(&many[i], 0);
    pthread_mutexattr_t a; pthread_mutexattr_init(&a); pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE); pthread_mutex_init(&recursive, &a);
    pthread_mutexattr_t f; pthread_mutexattr_init(&f); pthread_mutexattr_setpolicy_np(&f, PTHREAD_MUTEX_POLICY_FAIRSHARE_NP); pthread_mutex_init(&fair, &f);

    uint64_t t0 = mach_absolute_time();
    for (long i = 0; i < THREADS; ++i) pthread_create(&t[i], 0, hammer, (void*)(i + 1));
    for (int i = 0; i < THREADS; ++i) pthread_join(t[i], 0);
    uint64_t t1 = mach_absolute_time();
    long many_total = 0; for (int i = 0; i < 64; ++i) many_total += many_counter[i];
    printf("EXCLUSION: %d threads x 60000, counter %ld (expected %d), violations %ld, 64 small mutexes %ld, recursive %ld, fair-share %ld; %.2f s\n",
        THREADS, counter, THREADS * 60000, violations, many_total, rec_counter, fair_counter, (t1 - t0) / 1e9);
    if (counter != THREADS * 60000 || violations || many_total != THREADS * 60000 || rec_counter != THREADS * 7500 || fair_counter != THREADS * 7500) bad |= 1;

    for (long i = 0; i < 3; ++i) pthread_create(&t[i], 0, producer, 0);
    for (long i = 3; i < 7; ++i) pthread_create(&t[i], 0, consumer, 0);
    for (int i = 0; i < 3; ++i) pthread_join(t[i], 0);
    pthread_mutex_lock(&qlock); done_producing = 1; pthread_cond_broadcast(&qcond); pthread_mutex_unlock(&qlock);
    for (int i = 3; i < 7; ++i) pthread_join(t[i], 0);
    printf("CONDVAR: produced %ld consumed %ld (expected 60000 each), left %d\n", produced, consumed, queue_len);
    if (produced != 60000 || consumed != 60000 || queue_len != 0) bad |= 2;

    pthread_mutex_t e; pthread_mutexattr_t ea; pthread_mutexattr_init(&ea); pthread_mutexattr_settype(&ea, PTHREAD_MUTEX_ERRORCHECK); pthread_mutex_init(&e, &ea);
    pthread_mutex_lock(&e); int r1 = pthread_mutex_lock(&e), r2 = pthread_mutex_trylock(&big); pthread_mutex_unlock(&big); pthread_mutex_unlock(&e);
    printf("TYPES: errorcheck relock -> %d (expected %d), trylock free mutex -> %d\n", r1, EDEADLK, r2);
    if (r1 != EDEADLK || r2 != 0) bad |= 4;

    pthread_mutex_lock(&big);                       /* fork while another thread is queued on the mutex */
    pthread_t w; pthread_create(&w, 0, hammer, (void*)99); usleep(50000);
    pid_t pid = fork();
    if (pid == 0) { pthread_mutex_unlock(&big); pthread_mutex_lock(&big); pthread_mutex_unlock(&big); _exit(7); }
    int status = 0; waitpid(pid, &status, 0);
    pthread_mutex_unlock(&big); pthread_join(w, 0);
    printf("FORK: child used the contended mutex and exited with %d (expected 7)\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 7) bad |= 8;

    pthread_t p0, p1; pthread_create(&p0, 0, pinger, (void*)0); pthread_create(&p1, 0, pinger, (void*)1);
    t0 = mach_absolute_time(); sleep(2); ping_stop = 1; pthread_join(p0, 0); pthread_join(p1, 0); t1 = mach_absolute_time();
    printf("RESULT: %s (%d)\n", bad ? "FAILED" : "ok", bad);
    return bad;
}
