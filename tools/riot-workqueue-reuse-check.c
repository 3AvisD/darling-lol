#include <dispatch/dispatch.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
static size_t maximum_depth;
static int failure;
int main(int argc, char **argv) {
    dispatch_queue_t queue=dispatch_queue_create("org.darling.riot.reuse-check",0);
    dispatch_semaphore_t done=dispatch_semaphore_create(0);
    unsigned cycles=0;
    for(;cycles<400 && !failure;cycles++) {
        dispatch_async(queue,^{
            char marker;
            uintptr_t top=(uintptr_t)pthread_get_stackaddr_np(pthread_self());
            size_t depth=top-(uintptr_t)&marker;
            if(depth>maximum_depth) maximum_depth=depth;
            if(depth>128*1024) failure=1;
            dispatch_semaphore_signal(done);
        });
        dispatch_semaphore_wait(done,DISPATCH_TIME_FOREVER);
        usleep(10000);
    }
    printf("WORKQUEUE_REUSE cycles=%u maximum_stack_depth=%zu pass=%d\n",cycles,maximum_depth,!failure);
    if (argc>1 && !strcmp(argv[1], "--idle-restarts")) {
        for (unsigned restart=0; restart<3; ++restart) {
            // Workers retire after 15 seconds. Exercise requesting a new
            // worker after retirement, then synchronous queue handoff.
            sleep(16);
            dispatch_async(queue,^{ dispatch_semaphore_signal(done); });
            if (dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,5ull*1000000000))) {
                printf("FAIL idle worker restart=%u\n",restart); failure=1; break;
            }
            dispatch_sync(queue,^{});
        }
        printf("WORKQUEUE_IDLE_RESTARTS pass=%d\n",!failure);
    }
    dispatch_release(done);dispatch_release(queue);return failure;
}
