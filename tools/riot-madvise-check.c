#include <sys/mman.h>
#include <stdio.h>
#include <string.h>

// A standalone Darling process. Reads only its own Linux memory counters.
//
// The free hint is only forwarded for whole 2 MiB huge pages (see
// emulation/src/xnu_syscall/bsd/impl/mman/madvise.c), so the probe marks a
// 2 MiB-aligned region inside a larger mapping; an unaligned 4 MiB region would
// still be marked for the contained huge pages, but not for all of it.
extern int __linux_syscall(int number, ...);
#define HUGE_PAGE_SIZE (2ul * 1024 * 1024)

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
    unsigned char* mapping = mmap(NULL, size + HUGE_PAGE_SIZE, PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANON, -1, 0);
    if (mapping == MAP_FAILED) return 1;
    unsigned char* memory = (unsigned char*)(((unsigned long)mapping + HUGE_PAGE_SIZE - 1)
                                             & ~(HUGE_PAGE_SIZE - 1));
    memset(memory, 0x5a, size);
    unsigned long before = lazy_free_kib();
    if (before == (unsigned long)-1 || madvise(memory, size, 7) != 0) return 2;
    unsigned long freed = lazy_free_kib();
    if (madvise(memory, size, 8) != 0) return 3;
    unsigned long reused = lazy_free_kib();
    int contents = 1;
    for (size_t i = 0; i < size; ++i) if (memory[i] != 0x5a) { contents = 0; break; }
    int pass = freed >= before + size / 1024 && reused <= before && contents;
    printf("DARLING_MADVISE pass=%d reclaimable_KiB=%lu reuse_reclaimable_KiB=%lu contents=%s\n",
           pass, freed - before, reused - before, contents ? "preserved" : "changed");
    munmap(mapping, size + HUGE_PAGE_SIZE);
    return !pass;
}
