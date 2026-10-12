#include <sys/event.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

static mach_timebase_info_data_t scale;
static double milliseconds(uint64_t ticks) {
    return (double)ticks * scale.numer / scale.denom / 1000000.0;
}
int main(void) {
    mach_timebase_info(&scale);
    int queue=kqueue();
    if(queue<0)return 1;
    struct timespec immediate={0,0},limit={1,0};
    struct kevent64_s change={.ident=1,.filter=EVFILT_TIMER,
        .flags=EV_ADD|EV_ENABLE,.fflags=NOTE_NSECONDS,.data=16666667},event;
    if(kevent64(queue,&change,1,NULL,0,0,&immediate))return 2;
    uint64_t start=mach_absolute_time();unsigned ticks=0;
    while(ticks<60) {
        if(kevent64(queue,NULL,0,&event,1,0,&limit)!=1 ||
           event.filter!=EVFILT_TIMER || event.data<=0)return 3;
        ticks+=(unsigned)event.data;
    }
    double elapsed=milliseconds(mach_absolute_time()-start);
    change.flags=EV_DELETE;
    if(kevent64(queue,&change,1,NULL,0,0,&immediate))return 4;
    start=mach_absolute_time();
    change.flags=EV_ADD|EV_ENABLE|EV_ONESHOT;
    change.fflags=NOTE_MACHTIME|NOTE_ABSOLUTE;
    change.data=start+20000000ull*scale.denom/scale.numer;
    if(kevent64(queue,&change,1,&event,1,0,&limit)!=1 || event.data!=1)return 5;
    double deadline=milliseconds(mach_absolute_time()-start);
    int stale=kevent64(queue,NULL,0,&event,1,0,&immediate);
    close(queue);
    int pass=elapsed>=980 && elapsed<1100 && deadline>=19 && deadline<100 && stale==0;
    printf("KQUEUE_TIMER ticks=%u elapsed_ms=%.2f absolute_ms=%.2f stale=%d pass=%d\n",
        ticks,elapsed,deadline,stale,pass);
    return pass?0:6;
}
