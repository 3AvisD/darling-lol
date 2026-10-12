#include <mach/mach_time.h>
#include <stdio.h>
#include <stdint.h>
extern long __lolsys_syscall(long,long,long,long,long,long,int);
extern void __lolsys_set_wake_trace(int);
int main(void) {
    mach_timebase_info_data_t tb;mach_timebase_info(&tb);
    for(unsigned trace=0;trace<2;trace++) {
        __lolsys_set_wake_trace(trace);
        uint64_t start=mach_absolute_time();
        for(unsigned i=0;i<100000;i++) {
            /* Linux poll(NULL, 0, 0): exercise the traced wait path without IO. */
            if(__lolsys_syscall(0,0,0,0,0,0,7)!=0) return 1;
        }
        uint64_t ns=(mach_absolute_time()-start)*tb.numer/tb.denom;
        printf("SYSCALL_WAIT trace=%u iterations=100000 ns_per_call=%llu pass=1\n",trace,(unsigned long long)(ns/100000));
    }
    __lolsys_set_wake_trace(0);return 0;
}
