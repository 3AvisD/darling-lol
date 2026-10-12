#include <mach/mach.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdint.h>
extern long __lolsys_syscall(long,long,long,long,long,long,int);
static long mask(int how,uint64_t* in,uint64_t* out) {
    return __lolsys_syscall(how,(long)in,(long)out,sizeof(uint64_t),0,0,14);
}
int main(void) {
    uint64_t original=0,desired,observed=0;
    if(mask(0,NULL,&original))return 1;
    desired=original|(1ull<<49); /* A valid real-time signal unsupported by BSD. */
    if(mask(2,&desired,NULL))return 2;
    mach_timebase_info_data_t tb;mach_timebase_info(&tb);
    uint64_t start=mach_absolute_time();
    int failed=0;unsigned completed=0;
    for(unsigned cycle=0;cycle<1000;cycle++) {
        mach_port_t port=MACH_PORT_NULL;
        if(mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&port) ||
           mach_port_destroy(mach_task_self(),port)) {failed=1;break;}
        if(mask(0,NULL,&observed) || observed!=desired) {failed=1;break;}
        completed++;
    }
    uint64_t ns=(mach_absolute_time()-start)*tb.numer/tb.denom;
    mask(2,&original,NULL);
    printf("RPC_SIGNAL_MASK realtime_preserved=%d completed=%u ns_per_roundtrip=%llu pass=%d\n",
        observed==desired,completed,(unsigned long long)(ns/(completed?completed:1)),!failed);
    return failed?3:0;
}
