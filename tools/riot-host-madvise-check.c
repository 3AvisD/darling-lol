#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Links the actual Darling sys_madvise source against host syscall hooks.
// All memory belongs to this standalone check; no client/server is contacted.
long sys_madvise(void* address, unsigned long size, int advice);
static int lastAdvice = -1, conversions = 0;
long __lolsys_syscall(long a, long b, long c, long d, long e, long f, int nr) {
    if (nr == SYS_madvise) lastAdvice = (int)c;
    long result = syscall(nr, a, b, c, d, e, f);
    return result == -1 ? -errno : result;
}
int errno_linux_to_bsd(int error) {
    // EINVAL and ENOMEM have the same numeric values on these two systems.
    ++conversions;
    return error;
}
static void require(int condition, const char* message) {
    if (!condition) { fprintf(stderr, "HOST_MADVISE pass=0 error=%s\n", message); exit(1); }
}
static unsigned long lazyFreeKB(void) {
    FILE* file = fopen("/proc/self/smaps_rollup", "r");
    require(file != NULL, "smaps_rollup");
    char line[256]; unsigned long value = 0;
    while (fgets(line, sizeof(line), file)) if (sscanf(line, "LazyFree: %lu kB", &value) == 1) break;
    fclose(file); return value;
}
int main(int argc, char** argv) {
    const int oldImplementation = argc > 1;
    const size_t size = 4 * 1024 * 1024;
    unsigned char* memory = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    require(memory != MAP_FAILED, "anonymous mapping");
    memset(memory, 0x5a, size);
    unsigned long before = lazyFreeKB();
    require(sys_madvise(memory, size, 7) == 0, "Darwin free reusable");
    unsigned long marked = lazyFreeKB();
    if (!oldImplementation) {
        require(lastAdvice == 8, "Linux free translation");
        require(marked >= before + size / 1024, "pages not marked reclaimable");
    } else require(marked == before && lastAdvice == -1, "baseline behavior changed");
    require(sys_madvise(memory, size, 8) == 0, "Darwin reuse");
    unsigned long reused = lazyFreeKB();
    if (!oldImplementation) require(lastAdvice == 23 && reused == before, "reuse did not cancel lazy reclaim");
    for (size_t i = 0; i < size; ++i) require(memory[i] == 0x5a, "reuse changed surviving contents");
    memset(memory, 0x37, size);
    require(sys_madvise(memory, size, 5) == 0, "Darwin free");
    if (!oldImplementation) require(lastAdvice == 8 && lazyFreeKB() >= before + size / 1024, "plain free not reclaimable");
    require(sys_madvise(memory, size, 8) == 0, "second reuse");
    for (size_t i = 0; i < size; ++i) require(memory[i] == 0x37, "second reuse corrupted contents");
    require(sys_madvise(memory, size, 0) == 0 && lastAdvice == 0, "normal hint passthrough");
    if (!oldImplementation) {
        require(sys_madvise(memory + 1, size - 1, 7) == -EINVAL, "unaligned free error");
        require(sys_madvise(memory + 1, size - 1, 8) == -EINVAL, "unaligned reuse error");
        require(conversions == 2, "error translation hook");
    }
    require(munmap(memory, size) == 0, "unmap");
    printf("HOST_MADVISE pass=1 reclaimable_KiB=%lu reuse_reclaimable_KiB=%lu contents=preserved errors=ok\n", marked - before, reused - before);
    return 0;
}
