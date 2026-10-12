#include <signal.h>
#include <setjmp.h>
#include <stdio.h>

static sigjmp_buf recovery;
static volatile sig_atomic_t handled;

static void catch_fault(int signal_number)
{
    handled = signal_number == SIGSEGV;
    siglongjmp(recovery, 1);
}

__attribute__((naked, noinline)) static void invalid_context(void)
{
    __asm__("movq $0x40000000, %rbp\n"
            "movq %rbp, %rdi\n"
            "movq %rbp, %rsi\n"
            "xorq %rax, %rax\n"
            "movq (%rax), %rax\n"
            "retq\n");
}

__attribute__((noinline)) static void valid_frame_fault(void)
{
    __asm__ volatile("xorq %%rax, %%rax; movq (%%rax), %%rax" ::: "rax", "memory");
}

int main(void)
{
    signal(SIGSEGV, catch_fault);
    if (!sigsetjmp(recovery, 1)) invalid_context();
    int invalid_recovered = handled;
    handled = 0;
    if (!sigsetjmp(recovery, 1)) valid_frame_fault();
    printf("FAULT_RECOVERY invalid_frame_and_arguments=%d valid_frame=%d pass=%d\n",
           invalid_recovered, handled, invalid_recovered && handled);
    return invalid_recovered && handled ? 0 : 1;
}
