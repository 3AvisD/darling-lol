#include <mach/mach.h>
#include <mach/exception_types.h>
#include <mach/i386/thread_status.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "mach_excServer.h"

static mach_port_t exception_port;
static int handled;
static int server_ok = 1;
extern void fixture_fault_site(void);
extern void fixture_fault_resume(void);

__attribute__((naked, noinline)) static uint64_t fault(void) {
    __asm__("xorq %rax, %rax\n"
            ".globl _fixture_fault_site\n"
            "_fixture_fault_site:\n"
            "movq (%rax), %rax\n"
            ".globl _fixture_fault_resume\n"
            "_fixture_fault_resume:\n"
            "retq\n");
}

static kern_return_t recover(exception_type_t exception, int *flavor,
                             thread_state_t old_state, mach_msg_type_number_t old_count,
                             thread_state_t new_state, mach_msg_type_number_t *new_count) {
    if (exception != EXC_BAD_ACCESS || *flavor != x86_THREAD_STATE64 ||
        old_count != x86_THREAD_STATE64_COUNT || *new_count < old_count)
        return KERN_FAILURE;
    x86_thread_state64_t *state = (x86_thread_state64_t *)old_state;
    if (state->__rip != (uintptr_t)fixture_fault_site) return KERN_FAILURE;
    memcpy(new_state, old_state, old_count * sizeof(natural_t));
    state = (x86_thread_state64_t *)new_state;
    state->__rip = (uintptr_t)fixture_fault_resume;
    state->__rax = 0x12345678;
    *new_count = old_count;
    handled++;
    return KERN_SUCCESS;
}

kern_return_t catch_mach_exception_raise(mach_port_t port, mach_port_t thread,
    mach_port_t task, exception_type_t exception, mach_exception_data_t code,
    mach_msg_type_number_t count) {
    return KERN_FAILURE;
}

kern_return_t catch_mach_exception_raise_state(mach_port_t port,
    exception_type_t exception, const mach_exception_data_t code,
    mach_msg_type_number_t count, int *flavor, const thread_state_t old_state,
    mach_msg_type_number_t old_count, thread_state_t new_state,
    mach_msg_type_number_t *new_count) {
    return recover(exception, flavor, old_state, old_count, new_state, new_count);
}

kern_return_t catch_mach_exception_raise_state_identity(mach_port_t port,
    mach_port_t thread, mach_port_t task, exception_type_t exception,
    mach_exception_data_t code, mach_msg_type_number_t count, int *flavor,
    thread_state_t old_state, mach_msg_type_number_t old_count,
    thread_state_t new_state, mach_msg_type_number_t *new_count) {
    kern_return_t result = recover(exception, flavor, old_state, old_count, new_state, new_count);
    mach_port_deallocate(mach_task_self(), thread);
    mach_port_deallocate(mach_task_self(), task);
    return result;
}

static void *serve(void *unused) {
    for (int i = 0; i < 2; ++i) {
        if (mach_msg_server_once(mach_exc_server, 8192, exception_port, 0) != KERN_SUCCESS) {
            server_ok = 0;
            break;
        }
    }
    return NULL;
}

int main(void) {
    if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &exception_port) ||
        mach_port_insert_right(mach_task_self(), exception_port, exception_port, MACH_MSG_TYPE_MAKE_SEND))
        return 1;
    pthread_t server;
    if (pthread_create(&server, NULL, serve, NULL)) return 1;
    if (task_set_exception_ports(mach_task_self(), EXC_MASK_BAD_ACCESS, exception_port,
            EXCEPTION_STATE | MACH_EXCEPTION_CODES, x86_THREAD_STATE64)) return 1;
    int state_ok = fault() == 0x12345678;
    if (task_set_exception_ports(mach_task_self(), EXC_MASK_BAD_ACCESS, exception_port,
            EXCEPTION_STATE_IDENTITY | MACH_EXCEPTION_CODES, x86_THREAD_STATE64)) return 1;
    int identity_ok = fault() == 0x12345678;
    pthread_join(server, NULL);
    task_set_exception_ports(mach_task_self(), EXC_MASK_BAD_ACCESS, MACH_PORT_NULL,
                             EXCEPTION_DEFAULT, THREAD_STATE_NONE);
    mach_port_destroy(mach_task_self(), exception_port);
    printf("MACH_EXCEPTION_STATE state=%d identity=%d handled=%d server=%d\n",
           state_ok, identity_ok, handled, server_ok);
    return state_ok && identity_ok && handled == 2 && server_ok ? 0 : 1;
}
