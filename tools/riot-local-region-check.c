/* Check that mach_vm_region_recurse answered in the process is identical to darlingserver's answer, over the whole
 * address space and at addresses inside gaps, and how long each takes. */
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/mach_time.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
typedef kern_return_t (*region_fn)(vm_map_t, mach_vm_address_t*, mach_vm_size_t*, natural_t*, vm_region_recurse_info_t, mach_msg_type_number_t*);
static region_fn server;
static unsigned long mismatches, probes;
static int compare(mach_vm_address_t addr, mach_vm_address_t *next) {
    mach_vm_address_t a1 = addr, a2 = addr;
    mach_vm_size_t s1 = 123, s2 = 123;
    natural_t d1 = 7, d2 = 7;
    struct { uint32_t before; vm_region_submap_short_info_data_64_t info; uint32_t after; } i1, i2;
    memset(&i1, 0x5a, sizeof(i1)); memset(&i2, 0x5a, sizeof(i2));
    mach_msg_type_number_t c1 = VM_REGION_SUBMAP_SHORT_INFO_COUNT_64, c2 = c1;
    kern_return_t k1 = mach_vm_region_recurse(mach_task_self(), &a1, &s1, &d1, (vm_region_recurse_info_t)&i1.info, &c1);
    kern_return_t k2 = server(mach_task_self(), &a2, &s2, &d2, (vm_region_recurse_info_t)&i2.info, &c2);
    ++probes;
    if (k1 != k2 || a1 != a2 || s1 != s2 || d1 != d2 || c1 != c2 || memcmp(&i1, &i2, sizeof(i1)) ||
        i1.before != 0x5a5a5a5a || i1.after != 0x5a5a5a5a) {
        if (++mismatches <= 5) printf("MISMATCH at %llx: local kr=%d %llx+%llx depth=%u | server kr=%d %llx+%llx depth=%u\n",
            addr, k1, a1, s1, d1, k2, a2, s2, d2);
    }
    if (mismatches && probes <= 2) {
        for (size_t b = 0; b < sizeof(i1); ++b)
            if (((unsigned char*)&i1)[b] != ((unsigned char*)&i2)[b])
                printf("BYTE %lu: %02x vs %02x\n", b, ((unsigned char*)&i1)[b], ((unsigned char*)&i2)[b]);
    }
    /* every other count up to the long structure (19 words), with guard words around a 24-word buffer */
    for (mach_msg_type_number_t cnt = 1; cnt <= 19; cnt += (cnt == 11 ? 2 : 1)) {
        uint32_t w1[24], w2[24]; memset(w1, 0x5a, sizeof(w1)); memset(w2, 0x5a, sizeof(w2));
        mach_vm_address_t b1 = addr, b2 = addr; mach_vm_size_t z1 = 123, z2 = 123; natural_t e1 = 7, e2 = 7;
        mach_msg_type_number_t n1 = cnt, n2 = cnt;
        kern_return_t r1 = mach_vm_region_recurse(mach_task_self(), &b1, &z1, &e1, (vm_region_recurse_info_t)(w1 + 2), &n1);
        kern_return_t r2 = server(mach_task_self(), &b2, &z2, &e2, (vm_region_recurse_info_t)(w2 + 2), &n2);
        ++probes;
        if (r1 != r2 || b1 != b2 || z1 != z2 || e1 != e2 || n1 != n2 || memcmp(w1, w2, sizeof(w1))) {
            if (++mismatches <= 5) printf("MISMATCH count %u at %llx: local kr=%d %llx+%llx cnt=%u | server kr=%d %llx+%llx cnt=%u\n",
                cnt, addr, r1, b1, z1, n1, r2, b2, z2, n2);
        }
    }
    if (next) *next = a1 + s1;
    return k1;
}
int main(void) {
    server = (region_fn) dlsym(RTLD_DEFAULT, "__darling_rpc_mach_vm_region_recurse");
    if (!server) { printf("RESULT: no server stub exported\n"); return 1; }
    /* Warm both paths before scanning: lazy binding may itself add mappings. */
    compare((mach_vm_address_t)&server, NULL);
    unsigned long regions = 0;
    mach_vm_address_t addr = 0, next;
    while (compare(addr, &next) == KERN_SUCCESS) {
        ++regions;
        compare(next - 1, NULL);
        compare(next, NULL);
        if (next <= addr) { ++mismatches; break; }
        addr = next;
    }
    compare(UINT64_MAX, NULL);
    size_t page = getpagesize();
    char *mapping = mmap(NULL, 4 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (mapping == MAP_FAILED) return 2;
    compare((mach_vm_address_t)mapping, NULL);
    if (mprotect(mapping + page, page, PROT_READ) != 0) return 2;
    compare((mach_vm_address_t)(mapping + page), NULL);
    if (munmap(mapping + 2 * page, page) != 0) return 2;
    compare((mach_vm_address_t)(mapping + 2 * page), NULL);
    compare((mach_vm_address_t)(mapping + 3 * page), NULL);
    /* A child-only protection change catches a cached maps fd naming the parent's address space. */
    pid_t child = fork();
    if (child < 0) return 2;
    if (!child) {
        mismatches = 0;
        if (mprotect(mapping, page, PROT_NONE) != 0) _exit(2);
        compare((mach_vm_address_t)mapping, NULL);
        _exit(mismatches != 0);
    }
    int status;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status)) ++mismatches;
    munmap(mapping, 2 * page); munmap(mapping + 3 * page, page);
    FILE *file = tmpfile();
    if (!file || ftruncate(fileno(file), 4 * page)) return 2;
    mapping = mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_SHARED, fileno(file), page);
    if (mapping == MAP_FAILED) return 2;
    compare((mach_vm_address_t)mapping, NULL);
    munmap(mapping, page); fclose(file);
    printf("COMPARE: %lu regions, %lu queries, %lu mismatches (boundaries, errors, protection changes, gaps, shared file offset, fork)\n", regions, probes, mismatches);
    mach_vm_address_t a; mach_vm_size_t s; natural_t d; vm_region_submap_short_info_data_64_t info; mach_msg_type_number_t c;
    uint64_t t0 = mach_absolute_time();
    for (int i = 0; i < 20000; ++i) { a = (mach_vm_address_t)&server; d = 0; c = VM_REGION_SUBMAP_SHORT_INFO_COUNT_64; mach_vm_region_recurse(mach_task_self(), &a, &s, &d, (vm_region_recurse_info_t)&info, &c); }
    uint64_t t1 = mach_absolute_time();
    for (int i = 0; i < 20000; ++i) { a = (mach_vm_address_t)&server; d = 0; c = VM_REGION_SUBMAP_SHORT_INFO_COUNT_64; server(mach_task_self(), &a, &s, &d, (vm_region_recurse_info_t)&info, &c); }
    uint64_t t2 = mach_absolute_time();
    printf("BENCH: mach_vm_region_recurse %.2f us, server stub %.2f us per call\n", (t1 - t0) / 2e4 / 1e3, (t2 - t1) / 2e4 / 1e3);
    printf("RESULT: %s\n", mismatches ? "FAILED" : "ok");
    return mismatches != 0;
}
