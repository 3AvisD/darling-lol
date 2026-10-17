#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <cpuid.h>
#include <sched.h>
/* ORIGINAL_CANDIDATE */
static void native(uint32_t leaf, uint32_t sub, uint32_t r[4]) {
    __cpuid_count(leaf, sub, r[0], r[1], r[2], r[3]);
}
static uint64_t ns(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC_RAW, &t);
    return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
static int compare(const void* a, const void* b) {
    double x = *(const double*)a, y = *(const double*)b; return (x > y) - (x < y);
}
static double bench(int count) {
    double samples[9]; uint32_t r[4];
    for (int sample = 0; sample < 9; ++sample) {
        uint64_t begin = ns();
        for (int i = 0; i < count; ++i) native(1, 0, r);
        samples[sample] = (double)(ns() - begin) / count;
    }
    qsort(samples, 9, sizeof(double), compare); return samples[4];
}
static volatile sig_atomic_t observed_traps;
static void observe_trap(int sig, siginfo_t* info, void* context) {
    if (info && info->si_code == SI_KERNEL) ++observed_traps;
    mldr_sigsegv_cpuid_handler(sig, info, context);
}
static void previous(int sig) { _exit(42); }
static void* worker(void* unused) {
    uint32_t r[4]; native(0, 0, r);
    native(1, 0, r);
    return (void*)(uintptr_t)(r[0] == 0x000906ed);
}
static void fork_check(int real_fault) {
    pid_t child = fork();
    if (child == 0) {
        // Deliberately install a real existing handler before candidate setup.
        signal(SIGSEGV, previous);
        mldr_setup_cpuid_virtualization();
        /* Isolate non-CPUID signal chaining from CPUID support/capabilities. */
        syscall(SYS_arch_prctl, ARCH_SET_CPUID, 1);
        if (real_fault) {
            void* bad = (void*)syscall(SYS_mmap, NULL, 4096, 0, 0x22, -1, 0);
            if (bad == (void*)-1) _exit(98);
            *(volatile char*)bad = 1;
        } else raise(SIGSEGV);
        _exit(99);
    }
    if (child < 0) exit(1);
    int status; waitpid(child, &status, 0);
    printf("{\"check\":\"existing_sigsegv_handler\",\"real_page_fault\":%s,\"exit\":%d,\"signal\":%d,\"pass\":%s}\n",
           real_fault ? "true" : "false", WIFEXITED(status) ? WEXITSTATUS(status) : -1, WIFSIGNALED(status) ? WTERMSIG(status) : 0,
           WIFEXITED(status) && WEXITSTATUS(status) == 42 ? "true" : "false");
}
int main(void) {
    struct rlimit no_core = {0,0}; setrlimit(RLIMIT_CORE, &no_core);
    cpu_set_t affinity; CPU_ZERO(&affinity);
    if (!sched_getaffinity(0, sizeof(affinity), &affinity)) {
        for (int i=0; i<CPU_SETSIZE; ++i) if (CPU_ISSET(i, &affinity)) {
            CPU_ZERO(&affinity); CPU_SET(i, &affinity); sched_setaffinity(0,sizeof(affinity),&affinity); break;
        }
    }
    struct {const char* name; uint32_t leaf, sub, reg, bit;} features[] = {
        {"SSE3",1,0,2,0}, {"SSSE3",1,0,2,9}, {"FMA",1,0,2,12},
        {"SSE4_1",1,0,2,19}, {"SSE4_2",1,0,2,20}, {"AES",1,0,2,25},
        {"XSAVE",1,0,2,26}, {"OSXSAVE",1,0,2,27}, {"AVX",1,0,2,28}, {"F16C",1,0,2,29},
        {"VMX",1,0,2,5}, {"PCID",1,0,2,17}, {"x2APIC",1,0,2,21}, {"RDRAND",1,0,2,30}, {"BMI1",7,0,1,3}, {"HLE",7,0,1,4},
        {"AVX2",7,0,1,5}, {"BMI2",7,0,1,8}, {"RTM",7,0,1,11},
        {"AVX512ER",7,0,1,27}, {"AVX512F",7,0,1,16}, {"RDSEED",7,0,1,18}, {"ADX",7,0,1,19}, {"SHA",7,0,1,29},
        {"LZCNT",0x80000001u,0,2,5}, {"RDTSCP",0x80000001u,0,3,27}
    };
    for (size_t i=0; i<sizeof(features)/sizeof(features[0]); ++i) {
        uint32_t host[4], guest[4];
        native(features[i].leaf, features[i].sub, host);
        mldr_emulate_cpuid(features[i].leaf, features[i].sub, &guest[0],&guest[1],&guest[2],&guest[3]);
        uint32_t mask = 1u << features[i].bit;
        int extra = (guest[features[i].reg] & mask) && !(host[features[i].reg] & mask);
        printf("{\"check\":\"host_instruction_subset\",\"feature\":\"%s\",\"pass\":%s}\n",features[i].name,extra?"false":"true");
    }
    for (int row=0;row<7;++row) {
        const uint32_t leaves[]={1,1,7,7,7,0x80000001u,0x80000001u};
        const int regs[]={2,3,1,2,3,2,3};
        uint32_t host[4],guest[4]; native(leaves[row],0,host);
        mldr_emulate_cpuid(leaves[row],0,&guest[0],&guest[1],&guest[2],&guest[3]);
        uint32_t extra=guest[regs[row]] & ~host[regs[row]];
        printf("{\"check\":\"advertised_bits_missing_from_host\",\"leaf\":%u,\"register\":%d,\"extra_mask\":%u,\"pass\":%s}\n",leaves[row],regs[row],extra,extra?"false":"true");
    }
    uint32_t seven[4]; mldr_emulate_cpuid(7,0,&seven[0],&seven[1],&seven[2],&seven[3]);
    printf("{\"check\":\"avx512er_requires_avx512f\",\"pass\":%s}\n", !(seven[1]&(1u<<27)) || (seven[1]&(1u<<16)) ? "true":"false");
    uint32_t state[4], avx[4];
    mldr_emulate_cpuid(0xd,0,&state[0],&state[1],&state[2],&state[3]);
    mldr_emulate_cpuid(0xd,2,&avx[0],&avx[1],&avx[2],&avx[3]);
    printf("{\"check\":\"xsave_avx_component\",\"advertised\":%s,\"component_size\":%u,\"component_offset\":%u,\"pass\":%s}\n",
           state[0]&4 ? "true":"false",avx[0],avx[1], !(state[0]&4) || (avx[0]==256 && avx[1]>=576) ? "true":"false");
    ucontext_t context; memset(&context,0,sizeof(context));
    const uint8_t instruction[]={0x0f,0xa2};
    context.uc_mcontext.gregs[REG_RIP]=(greg_t)instruction;
    context.uc_mcontext.gregs[REG_RAX]=0xffffffff00000000ull;
    context.uc_mcontext.gregs[REG_RCX]=0xffffffff00000000ull;
    context.uc_mcontext.gregs[REG_EFL]=0x202;
    siginfo_t info; memset(&info,0,sizeof(info)); info.si_code=SI_KERNEL;
    mldr_sigsegv_cpuid_handler(SIGSEGV,&info,&context);
    int correct=context.uc_mcontext.gregs[REG_RIP]==(greg_t)(instruction+2) &&
        context.uc_mcontext.gregs[REG_RAX]==0x16 && context.uc_mcontext.gregs[REG_EFL]==0x202 &&
        (uint64_t)context.uc_mcontext.gregs[REG_RBX] <= UINT32_MAX;
    printf("{\"check\":\"registers_rip_flags\",\"pass\":%s}\n", correct?"true":"false");
    fork_check(0); fork_check(1);
    double baseline=bench(10000);
    printf("{\"check\":\"native_cpuid_benchmark\",\"median_ns_per_call\":%.2f,\"samples\":9,\"iterations_per_sample\":10000}\n",baseline);
    fflush(stdout);
    pid_t child=fork(); if(child<0)return 1;
    if(child==0) {
        errno=0; long result=syscall(SYS_arch_prctl, ARCH_SET_CPUID,0); int error=errno;
        if(result) {
            printf("{\"check\":\"hardware_faulting\",\"supported\":false,\"errno\":%d,\"trapped_benchmark\":\"unavailable\"}\n",error);
            fflush(stdout); _exit(0);
        }
        syscall(SYS_arch_prctl, ARCH_SET_CPUID,1);
        mldr_setup_cpuid_virtualization();
        struct sigaction observer; memset(&observer,0,sizeof(observer));
        observer.sa_sigaction=observe_trap; observer.sa_flags=SA_SIGINFO|SA_ONSTACK;
        sigemptyset(&observer.sa_mask); sigaction(SIGSEGV,&observer,NULL);
        uint32_t r[4]; native(1,0,r);
        int trapped=observed_traps>0 && r[0]==0x000906ed;
        native(0,0,r);
        pthread_t thread; void* ok=NULL;
        int created=pthread_create(&thread,NULL,worker,NULL);
        if(!created)pthread_join(thread,&ok);
        printf("{\"check\":\"hardware_faulting\",\"supported\":true,\"pass\":%s}\n",trapped?"true":"false");
        printf("{\"check\":\"thread_inheritance\",\"pass\":%s}\n",!created && ok?"true":"false");
        observer.sa_sigaction=mldr_sigsegv_cpuid_handler; sigaction(SIGSEGV,&observer,NULL);
        double latency=bench(10000);
        syscall(SYS_arch_prctl, ARCH_SET_CPUID,1);
        printf("{\"check\":\"trapped_cpuid_benchmark\",\"median_ns_per_call\":%.2f,\"ratio\":%.2f,\"samples\":9,\"iterations_per_sample\":10000}\n",latency,latency/baseline);
        fflush(stdout); _exit(0);
    }
    int status; waitpid(child,&status,0);
    if(!WIFEXITED(status)||WEXITSTATUS(status)) {
        printf("{\"check\":\"faulting_child_completed\",\"pass\":false,\"signal\":%d}\n",WIFSIGNALED(status)?WTERMSIG(status):0);
    }
    return 0;
}
