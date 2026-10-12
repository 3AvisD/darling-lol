/* Check of the user-space clock in libsystem_kernel (linux_premigration/ext/clock_gettime.c): accuracy against the real
 * Linux syscalls on every CPU, global monotonicity across threads that hop between CPUs, and cost per call. */
#include <mach/mach_time.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <elfcalls.h>
extern struct elf_calls* _elfcalls;
static long (*native_syscall)(long, ...);
static uint64_t ns(struct timespec ts) { return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec; }
static uint64_t shared_last;
static int backwards, ncpu = 12;
static void pin(int cpu) { unsigned long mask = 1ul << cpu; native_syscall(203L, 0L, sizeof(mask), &mask); }

static void* hopper(void* arg) {
    int id = (int)(long)arg;
    for (unsigned i = 0; i < 400000; ++i) {
        if (i % 2000 == 0) pin((id + i / 2000) % ncpu);
        uint64_t seen = __atomic_load_n(&shared_last, __ATOMIC_SEQ_CST);
        uint64_t now = mach_absolute_time();
        if (now < seen) __atomic_fetch_add(&backwards, 1, __ATOMIC_RELAXED);
        uint64_t last = seen;
        while (now > last && !__atomic_compare_exchange_n(&shared_last, &last, now, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {}
    }
    return NULL;
}

int main(void) {
    void* libc = _elfcalls->dlopen("libc.so.6");
    native_syscall = libc ? (long (*)(long, ...))_elfcalls->dlsym(libc, "syscall") : 0;
    if (!native_syscall) return 1;
    ncpu = (int)sysconf(_SC_NPROCESSORS_ONLN); if (ncpu < 1 || ncpu > 64) ncpu = 1;

    /* accuracy: the fast value must lie between two real reads, within a tolerance for the sampling error */
    int64_t worst_mono = 0, worst_real = 0; unsigned long outside = 0, total = 0;
    for (int round = 0; round < 3; ++round) for (int cpu = 0; cpu < ncpu; ++cpu) {
        pin(cpu);
        for (unsigned i = 0; i < 4000; ++i) {
            struct timespec a, b; struct timeval tv;
            native_syscall(228L, 1L, &a); uint64_t fast = mach_absolute_time(); native_syscall(228L, 1L, &b);
            int64_t err = fast < ns(a) ? (int64_t)(fast - ns(a)) : fast > ns(b) ? (int64_t)(fast - ns(b)) : 0;
            if (llabs(err) > llabs(worst_mono)) worst_mono = err;
            if (err) ++outside;
            native_syscall(228L, 0L, &a); gettimeofday(&tv, 0); native_syscall(228L, 0L, &b);
            uint64_t us = (uint64_t)tv.tv_sec * 1000000ull + tv.tv_usec;
            err = us < ns(a) / 1000 ? (int64_t)(us - ns(a) / 1000) : us > ns(b) / 1000 ? (int64_t)(us - ns(b) / 1000) : 0;
            if (llabs(err) > llabs(worst_real)) worst_real = err;
            ++total;
            if (i % 500 == 0) usleep(3000);
        }
    }
    printf("ACCURACY: %lu samples on %d CPUs, monotonic outside the syscall bracket %lu times, worst %lld ns; gettimeofday worst %lld us\n",
        total, ncpu, outside, (long long)worst_mono, (long long)worst_real);

    pthread_t threads[8];
    for (long i = 0; i < 8; ++i) if (pthread_create(&threads[i], 0, hopper, (void*)i)) return 2;
    for (int i = 0; i < 8; ++i) pthread_join(threads[i], 0);
    printf("MONOTONIC: 8 threads x 400000 reads hopping CPUs, went backwards %d times\n", backwards);

    /* long sleep: the per-CPU base is older than the window, the next read must resynchronise */
    pin(1); sleep(2);
    { struct timespec a, b; native_syscall(228L, 1L, &a); uint64_t fast = mach_absolute_time(); native_syscall(228L, 1L, &b);
      printf("AFTER SLEEP: inside bracket %d\n", fast + 20000 >= ns(a) && fast <= ns(b) + 20000); }

    volatile uint64_t sink = 0; struct timeval tv; struct timespec ts;
    uint64_t start = mach_absolute_time();
    for (unsigned i = 0; i < 2000000; ++i) sink = mach_absolute_time();
    uint64_t end = mach_absolute_time();
    printf("BENCH: mach_absolute_time %.1f ns/call\n", (end - start) / 2000000.0);
    start = mach_absolute_time();
    for (unsigned i = 0; i < 2000000; ++i) gettimeofday(&tv, 0);
    end = mach_absolute_time();
    printf("BENCH: gettimeofday %.1f ns/call\n", (end - start) / 2000000.0);
    start = mach_absolute_time();
    for (unsigned i = 0; i < 300000; ++i) clock_gettime(CLOCK_MONOTONIC, &ts);
    end = mach_absolute_time();
    printf("BENCH: clock_gettime(CLOCK_MONOTONIC) %.1f ns/call\n", (end - start) / 300000.0);
    (void)sink;
    return (backwards || llabs(worst_mono) > 50000 || llabs(worst_real) > 100) ? 3 : 0;
}
