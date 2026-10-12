#include <xpc/xpc.h>
#include <xpc/private/pipe.h>
#include <mach/mach.h>
#include <objc/runtime.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

extern void *objc_autoreleasePoolPush(void);
extern void objc_autoreleasePoolPop(void *);
extern void _CFEnableZombies(void);

static void *reply_server(void *context)
{
    mach_port_t listener = *(mach_port_t *)context;
    struct {
        mach_msg_header_t header;
        mach_msg_body_t body;
        mach_msg_port_descriptor_t ports[2];
        unsigned char padding[256];
    } checkin = {0};
    if (mach_msg(&checkin.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0,
                 sizeof(checkin), listener, 5000, MACH_PORT_NULL)) _exit(10);
    if (!(checkin.header.msgh_bits & MACH_MSGH_BITS_COMPLEX) ||
            checkin.body.msgh_descriptor_count != 2) _exit(11);
    mach_port_t incoming = MACH_PORT_NULL, unused_send = MACH_PORT_NULL;
    for (unsigned i = 0; i < 2; i++) {
        if (checkin.ports[i].disposition == MACH_MSG_TYPE_PORT_RECEIVE)
            incoming = checkin.ports[i].name;
        else if (checkin.ports[i].disposition == MACH_MSG_TYPE_PORT_SEND)
            unused_send = checkin.ports[i].name;
    }
    if (!incoming) _exit(12);
    for (unsigned i = 0; i < 100; i++) {
        void *pool = objc_autoreleasePoolPush();
        xpc_object_t request = NULL;
        if (xpc_pipe_receive(incoming, &request, 0)) _exit(13);
        xpc_object_t reply = xpc_dictionary_create_reply(request);
        if (!reply || xpc_pipe_routine_reply(reply)) _exit(14);
        xpc_release(reply);
        xpc_release(request);
        objc_autoreleasePoolPop(pool);
    }
    mach_port_destroy(mach_task_self(), incoming);
    if (unused_send) mach_port_deallocate(mach_task_self(), unused_send);
    return NULL;
}

int main(void)
{
    /* Keep a freed object identifiable so the baseline fails deterministically. */
    _CFEnableZombies();
    mach_port_t port = MACH_PORT_NULL;
    if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port)) return 3;
    if (mach_port_insert_right(mach_task_self(), port, port, MACH_MSG_TYPE_MAKE_SEND)) return 4;
    for (unsigned mode = 0; mode < 2; mode++) {
        for (unsigned i = 0; i < 100; i++) {
            struct {
                mach_msg_header_t header;
                unsigned int wire[5];
            } message = {0};
            message.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
            message.header.msgh_size = sizeof(message);
            message.header.msgh_remote_port = port;
            message.header.msgh_id = 0x10000000;
            /* XPC v5 with an empty dictionary. */
            unsigned int wire[] = {0x40585043, 5, 0x0f000, 4, 0};
            memcpy(message.wire, wire, sizeof(wire));
            if (mach_msg(&message.header, MACH_SEND_MSG, sizeof(message), 0,
                         MACH_PORT_NULL, 0, MACH_PORT_NULL)) return 5;
            void *pool = objc_autoreleasePoolPush();
            xpc_object_t received = NULL;
            mach_port_t reply_port = MACH_PORT_NULL;
            int status = mode ? xpc_pipe_try_receive(port, &received, &reply_port,
                                  NULL, sizeof(mach_msg_header_t), 0)
                              : xpc_pipe_receive(port, &received, 0);
            objc_autoreleasePoolPop(pool);
            if (status || !received || object_getClass((id)received) != (Class)XPC_TYPE_DICTIONARY) {
                printf("XPC_PIPE_OWNERSHIP mode=%u iteration=%u pass=0\n", mode, i);
                fflush(stdout);
                return 2;
            }
            if (xpc_dictionary_get_count(received) != 0) return 6;
            xpc_release(received);
        }
    }
    pthread_t server;
    if (pthread_create(&server, NULL, reply_server, &port)) return 7;
    xpc_pipe_t pipe = xpc_pipe_create_from_port(port, 0);
    if (!pipe) return 8;
    for (unsigned i = 0; i < 100; i++) {
        void *pool = objc_autoreleasePoolPush();
        xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0), reply = NULL;
        int status = xpc_pipe_routine(pipe, request, &reply);
        xpc_release(request);
        objc_autoreleasePoolPop(pool);
        if (status || !reply || object_getClass((id)reply) != (Class)XPC_TYPE_DICTIONARY) {
            printf("XPC_PIPE_OWNERSHIP reply=%u pass=0\n", i);
            fflush(stdout);
            return 9;
        }
        xpc_release(reply);
    }
    pthread_join(server, NULL);
    xpc_release(pipe);
    mach_port_destroy(mach_task_self(), port);
    printf("XPC_PIPE_OWNERSHIP receives=200 replies=100 pass=1\n");
    fflush(stdout);
    return 0;
}
