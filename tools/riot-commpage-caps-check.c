#define PRIVATE
#include <i386/cpu_capabilities.h>
#include "../build/darling-src/src/startup/mldr/commpage-cpu-caps.h"
#include <stdio.h>

static void cpu(unsigned leaf, unsigned subleaf, unsigned* a, unsigned* b, unsigned* c, unsigned* d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(subleaf));
}
int main(void) {
    unsigned a,b,c,d,ecx1,edx1,ebx7=0;
    cpu(0,0,&a,&b,&c,&d); unsigned max=a;
    cpu(1,0,&a,&b,&ecx1,&edx1);
    if (max>=7) cpu(7,0,&a,&ebx7,&c,&d);
    uint64_t xcr0=0;
    if ((ecx1 & ((1u<<26)|(1u<<27))) == ((1u<<26)|(1u<<27))) {
        __asm__ volatile("xgetbv" : "=a"(a), "=d"(d) : "c"(0));
        xcr0=((uint64_t)d<<32)|a;
    }
    cpu(0x80000001u,0,&a,&b,&c,&d);
    const uint64_t expected=commpage_decode_cpu_caps(ecx1,edx1,ebx7,xcr0,!!(d&(1u<<29)));
    const uint64_t actual=*(volatile const uint64_t*)_COMM_PAGE_CPU_CAPABILITIES64;
    printf("CPU_CAPS actual=%llx expected=%llx %s\n",(unsigned long long)actual,(unsigned long long)expected,actual==expected?"PASS":"FAIL");
    return actual!=expected;
}
