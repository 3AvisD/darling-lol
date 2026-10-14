// Host-only check for the darling-lol vDSO clock path in
// emulation/src/linux_premigration/ext/clock_gettime.c.
//
// The real clock source is compiled against a stub ELF bridge backed by the
// host's dlopen/dlsym, so the resolver, the fallback and the steady-state call
// all run for real. Only this process's own memory is touched; no guest, client
// or Darling namespace is involved.
//
// Modes (argv[1]):
//   (none)       null bridge first, then a working bridge
//   no-symbol    the bridge exists but has no __vdso_clock_gettime entry
//   race         many threads resolve and read the clock at the same time
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <elfcalls.h>

// ---------------------------------------------------------------------------
// Syscall shim for the compiled emulation source (LINUX_SYSCALL -> __lolsys_syscall).

static unsigned long clockSyscalls = 0; // __NR_clock_gettime through the shim

long __lolsys_syscall(long a1, long a2, long a3, long a4, long a5, long a6, int nr) {
    if (nr == SYS_clock_gettime)
        ++clockSyscalls;
    if (nr == SYS_access && a1 && strcmp((const char*)a1, "/tmp/rclol-fast-clock") == 0)
        return -ENOENT; // keep the opt-in TSC path out of the test
    long result = syscall(nr, a1, a2, a3, a4, a5, a6);
    return result == -1 ? -errno : result;
}

void cerror(int error) {
    errno = error;
}

int errno_linux_to_bsd(int error) {
    return error;
}

// ---------------------------------------------------------------------------
// Stub ELF bridge that mirrors mldr's elfcalls: dlopen is the host loader.

static int dlopenCalls = 0, dlsymCalls = 0;
static int bridgeHasSymbol = 1;

static void* bridgeDlopen(const char* name) {
    ++dlopenCalls;
    return dlopen(name, RTLD_LAZY);
}

static void* bridgeDlsym(void* library, const char* name) {
    ++dlsymCalls;
    if (!bridgeHasSymbol)
        return 0;
    return dlsym(library, name);
}

static struct elf_calls bridge;
static struct elf_calls* hostElfcalls = 0; // what the compiled source sees as _elfcalls

// The compiled clock source refers to these.
extern int __linux_clock_gettime(int clockid, struct timespec* res);
extern int __darling_vdso_clock_gettime(int clockid, struct timespec* res);
extern void __darling_vdso_clock_init(void);
struct elf_calls* _elfcalls = 0;

// ---------------------------------------------------------------------------

static void fail(const char* message) {
    fprintf(stderr, "HOST_VDSO_CLOCK pass=0 error=%s (errno=%d)\n", message, errno);
    exit(1);
}

static void require(int condition, const char* message) {
    if (!condition)
        fail(message);
}

static uint64_t rawNsec(int clockid) {
    struct timespec ts;
    long result = syscall(SYS_clock_gettime, clockid, &ts);
    require(result == 0, "raw syscall");
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t nsec(const struct timespec* ts) {
    return (uint64_t)ts->tv_sec * 1000000000ull + (uint64_t)ts->tv_nsec;
}

static struct timespec sink;

struct threadArgs {
    int rounds;
    int errors;
    int backwards;
};

static void* threadBody(void* raw) {
    struct threadArgs* args = (struct threadArgs*)raw;
    uint64_t last = 0;
    for (int i = 0; i < args->rounds; ++i) {
        struct timespec ts;
        if (__linux_clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
            ++args->errors;
            continue;
        }
        uint64_t current = nsec(&ts);
        if (current < last)
            ++args->backwards;
        last = current;
    }
    return 0;
}

static double nsPerCall(void (*body)(void), int rounds) {
    uint64_t begin = rawNsec(CLOCK_MONOTONIC);
    for (int i = 0; i < rounds; ++i)
        body();
    uint64_t finish = rawNsec(CLOCK_MONOTONIC);
    return (double)(finish - begin) / rounds;
}

static void bodySyscall(void) {
    syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &sink);
}

static void bodyWrapper(void) {
    __linux_clock_gettime(CLOCK_MONOTONIC, &sink);
}

static void runThreads(int count, int rounds) {
    pthread_t threads[8];
    struct threadArgs args[8];
    require(count <= 8, "thread count");
    for (int i = 0; i < count; ++i) {
        args[i].rounds = rounds;
        args[i].errors = 0;
        args[i].backwards = 0;
        require(pthread_create(&threads[i], 0, threadBody, &args[i]) == 0, "pthread_create");
    }
    for (int i = 0; i < count; ++i)
        pthread_join(threads[i], 0);
    for (int i = 0; i < count; ++i) {
        require(args[i].errors == 0, "threaded clock read failed");
        require(args[i].backwards == 0, "threaded clock read went backwards");
    }
}

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "default";
    int race = strcmp(mode, "race") == 0;
    int noSymbol = strcmp(mode, "no-symbol") == 0;
    bridge.dlopen = bridgeDlopen;
    bridge.dlsym = bridgeDlsym;
    bridge.dlclose = 0;
    struct timespec ts;

    if (race) {
        // Resolution has not run yet: several threads resolve and read at once.
        _elfcalls = &bridge;
        runThreads(8, 20000);
        require(dlopenCalls >= 1, "race did not resolve");
        int resolvedDlopenCalls = dlopenCalls;
        unsigned long resolvedSyscalls = clockSyscalls;
        require(__darling_vdso_clock_gettime(CLOCK_MONOTONIC, &ts) == 0, "race did not produce a usable entry");
        require(dlopenCalls == resolvedDlopenCalls, "resolution ran again after the race");
        require(clockSyscalls == resolvedSyscalls, "resolved entry still used the syscall");
    } else {
        // A clock read before the bridge is set must fall back to the syscall
        // and must not cache "unavailable".
        unsigned long before = clockSyscalls;
        require(__linux_clock_gettime(CLOCK_MONOTONIC, &ts) == 0, "null-bridge fallback");
        require(clockSyscalls == before + 1, "null bridge did not use the syscall");
        uint64_t value = nsec(&ts), reference = rawNsec(CLOCK_MONOTONIC);
        require(value <= reference && reference - value < 50000000ull, "fallback value disagrees with the syscall");

        // The bridge appears: the next call resolves the vDSO entry and uses it.
        bridgeHasSymbol = !noSymbol;
        _elfcalls = &bridge;
        unsigned long syscallsBeforeInit = clockSyscalls;
        __darling_vdso_clock_init();
        require(dlopenCalls >= 1, "eager init did not resolve");
        require(clockSyscalls == syscallsBeforeInit, "eager init made a clock syscall");
        int resolvedDlopenCalls = dlopenCalls;

        if (noSymbol) {
            require(__darling_vdso_clock_gettime(CLOCK_MONOTONIC, &ts) != 0, "missing symbol reported success");
            before = clockSyscalls;
            require(__linux_clock_gettime(CLOCK_MONOTONIC, &ts) == 0, "no-symbol fallback");
            require(clockSyscalls == before + 1, "no-symbol path did not use the syscall");
        } else {
            require(__darling_vdso_clock_gettime(CLOCK_MONOTONIC, &ts) == 0, "resolved vDSO entry was not used");
            before = clockSyscalls;
            require(__linux_clock_gettime(CLOCK_MONOTONIC, &ts) == 0, "vDSO read");
            require(clockSyscalls == before, "vDSO read still used the syscall");
            value = nsec(&ts);
            reference = rawNsec(CLOCK_MONOTONIC);
            require(value <= reference && reference - value < 50000000ull, "vDSO value disagrees with the syscall");

            // Values agree with the syscall across clock ids and the monotonic
            // clock is monotonic across a million reads.
            for (int clockid = 0; clockid <= 1; ++clockid) {
                uint64_t last = 0;
                for (int i = 0; i < 1000000; ++i) {
                    require(__linux_clock_gettime(clockid, &ts) == 0, "million reads");
                    uint64_t current = nsec(&ts);
                    if (clockid == 1)
                        require(current >= last, "monotonic clock went backwards");
                    last = current;
                }
                reference = rawNsec(clockid);
                require(last <= reference && reference - last < 100000000ull, "clock drifted from the syscall");
            }

            // Several threads reading at once stay monotonic within each thread.
            runThreads(8, 20000);
            require(dlopenCalls == resolvedDlopenCalls, "resolution ran more than once");

            // After fork the child inherits the resolved pointer with no new
            // resolution; the vDSO mapping is the same in both processes.
            unsigned long baselineSyscalls = clockSyscalls;
            pid_t child = fork();
            require(child >= 0, "fork");
            if (child == 0) {
                struct timespec childTs;
                int ok = __linux_clock_gettime(CLOCK_MONOTONIC, &childTs) == 0
                        && clockSyscalls == baselineSyscalls && dlopenCalls == resolvedDlopenCalls;
                _exit(ok ? 0 : 1);
            }
            int status = 0;
            require(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
                    "forked child re-resolved or failed");
        }
    }

    // Cost comparison: a raw syscall against the wrapper's steady-state path.
    double syscallCost = nsPerCall(bodySyscall, 200000);
    double wrapperCost = nsPerCall(bodyWrapper, 200000);
    if (!noSymbol) {
        require(wrapperCost < syscallCost / 2, "vDSO path is not cheaper than the syscall");
    }
    printf("HOST_VDSO_CLOCK pass=1 mode=%s syscall_ns=%.0f vdso_ns=%.0f dlopen_calls=%d dlsym_calls=%d "
           "million_reads=%d threads=%d fork=preserved\n",
           mode, syscallCost, wrapperCost, dlopenCalls, dlsymCalls,
           (race || noSymbol) ? 0 : 1, noSymbol ? 0 : 8);
    return 0;
}
