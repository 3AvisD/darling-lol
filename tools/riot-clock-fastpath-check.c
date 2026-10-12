#include <mach/mach_time.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <elfcalls.h>
extern struct elf_calls* _elfcalls;
static int failed;
static long (*native_syscall)(long,...);
static uint64_t ns(struct timespec ts) { return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec; }
static void* worker(void* unused) {
    uint64_t previous = mach_absolute_time();
    for (unsigned i=0; i<100000; ++i) {
        uint64_t now=mach_absolute_time();
        if(now < previous) { __atomic_store_n(&failed,1,__ATOMIC_RELAXED); return NULL; }
        previous=now;
    }
    return NULL;
}
int main(void) {
    void* libc=_elfcalls->dlopen("libc.so.6");
    native_syscall=libc ? (long (*)(long,...))_elfcalls->dlsym(libc,"syscall") : 0;
    if(!native_syscall) return 1;
    void* vdso=_elfcalls->dlopen("linux-vdso.so.1");
    void* (*lookup)(void*,const char*,const char*)=(void* (*)(void*,const char*,const char*))_elfcalls->dlsym(libc,"dlvsym");
    int (*fast)(int,struct timespec*)=lookup && vdso ? (int (*)(int,struct timespec*))lookup(vdso,"__vdso_clock_gettime","LINUX_2.6") : 0;
    printf("CHECK: native vDSO handle=%d lookup=%d function=%d\n",vdso!=0,lookup!=0,fast!=0);
    if (fast) { struct timespec check; printf("CHECK: native vDSO clock result=%d\n",fast(1,&check)); }

    mach_timebase_info_data_t tb; mach_timebase_info(&tb);
    if(tb.numer!=1 || tb.denom!=1) return 2;
    for(unsigned i=0;i<10000;++i) {
        uint64_t before=mach_absolute_time();
        struct timespec reference;
        if(native_syscall(228L,1,&reference)) return 3;
        uint64_t after=mach_absolute_time();
        if(before > ns(reference) || after < ns(reference)) return 4;
        struct timeval a,b;
        if(gettimeofday(&a,0) || native_syscall(228L,0,&reference) || gettimeofday(&b,0)) return 5;
        uint64_t ref=ns(reference)/1000;
        if((uint64_t)a.tv_sec*1000000+a.tv_usec > ref || (uint64_t)b.tv_sec*1000000+b.tv_usec < ref) return 6;
    }
    pthread_t threads[4];
    for(unsigned i=0;i<4;++i) if(pthread_create(&threads[i],0,worker,0)) return 7;
    for(unsigned i=0;i<4;++i) if(pthread_join(threads[i],0)) return 8;
    if(failed) return 9;
    struct timespec invalid;
    errno=0;
    if(clock_gettime((clockid_t)-123,&invalid)!=-1 || errno!=EINVAL) return 10;
    volatile uint64_t sink=0;
    uint64_t start=mach_absolute_time();
    for(unsigned i=0;i<1000000;++i) sink=mach_absolute_time();
    uint64_t end=mach_absolute_time();
    printf("BENCH: mach_absolute_time %.1f ns/call\n",(end-start)/1000000.0);
    struct timeval tv;
    start=mach_absolute_time();
    for(unsigned i=0;i<1000000;++i) if(gettimeofday(&tv,0)) return 11;
    end=mach_absolute_time();
    printf("BENCH: gettimeofday %.1f ns/call\n",(end-start)/1000000.0);
    printf("PASS: clocks bracket raw Linux syscalls, 4 concurrent monotonic readers, invalid clock errno\n");
    return sink==0;
}
