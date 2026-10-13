#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// A standalone Darling process. Reads only its own Linux memory counters.
extern int __linux_syscall(int number, ...);
static unsigned long lazy_free_kib(void) {
    char text[4096];
    int fd = __linux_syscall(257, -100, "/proc/self/smaps_rollup", 0, 0);
    if (fd < 0) return (unsigned long)-1;
    int size = __linux_syscall(0, fd, text, sizeof(text) - 1);
    __linux_syscall(3, fd);
    if (size <= 0) return (unsigned long)-1;
    text[size] = 0;
    char* field = strstr(text, "LazyFree:");
    unsigned long value = 0;
    return field && sscanf(field, "LazyFree: %lu kB", &value) == 1 ? value : (unsigned long)-1;
}
int main(void) {
    const size_t size = 4 * 1024 * 1024;
    unsigned char* memory = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (memory == MAP_FAILED) return 1;
    memset(memory, 0x5a, size);
    unsigned long before = lazy_free_kib();
    if (before == (unsigned long)-1 || madvise(memory, size, 7) != 0) return 2;
    unsigned long freed = lazy_free_kib();
    if (madvise(memory, size, 8) != 0) return 3;
    unsigned long reused = lazy_free_kib();
    int contents = 1;
    for (size_t i = 0; i < size; ++i) if (memory[i] != 0x5a) { contents = 0; break; }
    munmap(memory, size);
    int pass = freed >= before + size / 1024 && reused <= before && contents;
    printf("DARLING_MADVISE pass=%d reclaimable_KiB=%lu reuse_reclaimable_KiB=%lu contents=%s\n", pass, freed - before, reused - before, contents ? "preserved" : "changed");
    return !pass;
}
