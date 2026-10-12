#define PRIVATE
#include <i386/cpu_capabilities.h>
#include "../build/darling-src/src/startup/mldr/commpage-cpu-caps.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(commpage_decode_cpu_caps(0, 0, 0, 0, 0) == 0);
    assert(commpage_decode_cpu_caps(1u << 26, 0, 0, 0, 0) == 0);
    assert(commpage_decode_cpu_caps(0, 1u << 26, 0, 0, 0) == kHasSSE2);
    assert(commpage_decode_cpu_caps(1u << 20, 0, 0, 0, 0) == kHasSSE4_2);
    assert(commpage_decode_cpu_caps(0, 1u << 20, 0, 0, 0) == 0);
    const uint32_t avx = (1u << 26) | (1u << 27) | (1u << 28) | (1u << 12) | (1u << 29);
    const uint64_t vector_caps = kHasAVX1_0 | kHasAVX2_0 | kHasFMA | kHasF16C;
    assert(commpage_decode_cpu_caps(avx, 0, 1u << 5, 6, 0) == vector_caps);
    assert(commpage_decode_cpu_caps(avx, 0, 1u << 5, 2, 0) == 0);
    assert(commpage_decode_cpu_caps(avx & ~(1u << 27), 0, 1u << 5, 6, 0) == 0);
    assert(commpage_decode_cpu_caps(avx & ~(1u << 26), 0, 1u << 5, 6, 0) == 0);
    assert(commpage_decode_cpu_caps(0, 0, 1u << 3, 0, 1) == (kHasBMI1 | k64Bit));
    puts("PASS: distinct CPUID registers and AVX operating-system state gates");
    return 0;
}
