#include <malloc/malloc.h>
#include <dlfcn.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>

static _Atomic unsigned failures;
static void *transferred[4][256];
static const size_t sizes[] = {16, 64, 256, 2048, 16384, 262144};

static void *worker(void *argument) {
    unsigned index = (unsigned)(uintptr_t)argument;
    for (unsigned cycle = 0; cycle < 2000; ++cycle) {
        size_t size = sizes[cycle % 6];
        unsigned char pattern = (unsigned char)(cycle + index);
        unsigned char *data = calloc(1, size);
        if (!data) { ++failures; break; }
        for (size_t i = 0; i < size; ++i)
            if (data[i]) { ++failures; break; }
        if (malloc_size(data) < size) ++failures;
        memset(data, pattern, size);
        unsigned char *resized = realloc(data, size + 37);
        if (!resized) { ++failures; free(data); break; }
        for (size_t i = 0; i < size; ++i)
            if (resized[i] != pattern) { ++failures; break; }
        free(resized);
        void *aligned = NULL;
        if (posix_memalign(&aligned, 256, size) || ((uintptr_t)aligned & 255)) ++failures;
        free(aligned);
    }
    for (unsigned i = 0; i < 256; ++i) {
        unsigned char *data = malloc(4096);
        transferred[index][i] = data;
        if (data) memset(data, (unsigned char)index, 4096);
        else ++failures;
    }
    return NULL;
}

static void *cross_free(void *argument) {
    unsigned index = (unsigned)(uintptr_t)argument;
    for (unsigned i = 0; i < 256; ++i) {
        unsigned char *data = transferred[index][i];
        if (!data) continue;
        for (unsigned j = 0; j < 4096; ++j)
            if (data[j] != index) { ++failures; break; }
        free(data);
    }
    return NULL;
}

int main(void) {
    Dl_info info = {0};
    int isolated = dladdr((void *)malloc, &info) && info.dli_fname &&
                   strstr(info.dli_fname, "riot-malloc-check-runtime") != NULL;
    pthread_t threads[4];
    for (unsigned i = 0; i < 4; ++i)
        if (pthread_create(&threads[i], NULL, worker, (void *)(uintptr_t)i)) return 2;
    for (unsigned i = 0; i < 4; ++i) pthread_join(threads[i], NULL);
    for (unsigned i = 0; i < 4; ++i)
        if (pthread_create(&threads[i], NULL, cross_free, (void *)(uintptr_t)i)) return 3;
    for (unsigned i = 0; i < 4; ++i) pthread_join(threads[i], NULL);
    pid_t child = fork();
    if (child == 0) {
        for (unsigned i = 0; i < 2000; ++i) {
            unsigned char *data = calloc(1, 4096);
            if (!data) _exit(7);
            for (unsigned j = 0; j < 4096; ++j) if (data[j]) _exit(8);
            free(data);
        }
        _exit(0);
    }
    int childStatus = 0;
    if (child < 0 || waitpid(child, &childStatus, 0) != child || !WIFEXITED(childStatus) || WEXITSTATUS(childStatus)) ++failures;
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    uint64_t start = mach_absolute_time();
    for (unsigned i = 0; i < 100000; ++i) {
        void *data = malloc(128 + (i & 255));
        if (!data) return 4;
        __asm__ volatile("" : : "r"(data) : "memory");
        memset(data, (int)i, 128 + (i & 255));
        __asm__ volatile("" : : "r"(data) : "memory");
        free(data);
    }
    uint64_t nanoseconds = (mach_absolute_time() - start) * timebase.numer / timebase.denom;
    printf("MALLOC_INTEGRITY isolated_candidate=%d threaded_cycles=8000 cross_thread_frees=1024 fork_checked=1 failures=%u ns_per_pair=%llu pass=%d\n",
           isolated, (unsigned)failures, (unsigned long long)(nanoseconds / 100000), !failures);
    return failures ? 1 : 0;
}
