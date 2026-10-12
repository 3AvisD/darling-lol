#include <dispatch/dispatch.h>
#include <stdio.h>
#include <unistd.h>
int main(void) {
    dispatch_queue_t queue=dispatch_queue_create("org.darling.riot.fd-check",0);
    dispatch_semaphore_t delivered=dispatch_semaphore_create(0);
    dispatch_semaphore_t canceled=dispatch_semaphore_create(0);
    for(unsigned cycle=0;cycle<40;cycle++) {
        int fds[2];if(pipe(fds)) return 1;
        __block unsigned seenA=0,seenB=0;
        dispatch_source_t a=dispatch_source_create(DISPATCH_SOURCE_TYPE_READ,fds[0],0,queue);
        dispatch_source_t b=dispatch_source_create(DISPATCH_SOURCE_TYPE_READ,fds[0],0,queue);
        /* Keep the pipe readable until both owners receive their event. Draining
         * it in the first handler makes the second notification timing-dependent. */
        dispatch_source_set_event_handler(a,^{if(!seenA++) dispatch_semaphore_signal(delivered); });
        dispatch_source_set_event_handler(b,^{if(!seenB++) dispatch_semaphore_signal(delivered); });
        dispatch_source_set_cancel_handler(a,^{dispatch_semaphore_signal(canceled);});
        dispatch_source_set_cancel_handler(b,^{dispatch_semaphore_signal(canceled);});
        dispatch_resume(a);dispatch_resume(b);
        write(fds[1],"x",1);
        int failed=dispatch_semaphore_wait(delivered,dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC))!=0;
        failed|=dispatch_semaphore_wait(delivered,dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC))!=0;
        dispatch_source_cancel(a);dispatch_source_cancel(b);
        failed|=dispatch_semaphore_wait(canceled,dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC))!=0;
        failed|=dispatch_semaphore_wait(canceled,dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC))!=0;
        dispatch_release(a);dispatch_release(b);close(fds[0]);close(fds[1]);
        if(failed || !seenA || !seenB) {printf("DISPATCH_FD overlap=fail cycle=%u first=%u second=%u\n",cycle,seenA,seenB);return 2;}
    }
    puts("DISPATCH_FD overlap=pass cancellation=pass descriptor_reuse=pass cycles=40");
    dispatch_release(canceled);dispatch_release(delivered);dispatch_release(queue);return 0;
}
