#include <sys/event.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
int main(void) {
    int kq=kqueue();
    struct timespec immediate={0,0};
    for (unsigned cycle=0;cycle<1000;cycle++) {
        int fd[2]; if(socketpair(AF_UNIX,SOCK_STREAM,0,fd)) return 1;
        struct kevent64_s change={.ident=fd[0],.filter=EVFILT_WRITE,
            .flags=EV_ADD|EV_DISPATCH2,.udata=cycle+1},event;
        if(kevent64(kq,&change,1,NULL,0,0,&immediate)) return 2;
        if(kevent64(kq,NULL,0,&event,1,0,&immediate)!=1 || event.udata!=cycle+1) return 3;
        change.flags=EV_DELETE|EV_UDATA_SPECIFIC|EV_RECEIPT;
        if(kevent64(kq,&change,1,&event,1,0,&immediate)!=1 ||
           !(event.flags&EV_ERROR) || event.data!=EINPROGRESS) {
            printf("DISPATCH2 deferred_delete=fail cycle=%u error=%lld\n",cycle,event.data);return 4;
        }
        change.flags=EV_ENABLE|EV_UDATA_SPECIFIC|EV_RECEIPT;
        if(kevent64(kq,&change,1,&event,1,0,&immediate)!=1 || event.data!=0) return 5;
        if(kevent64(kq,NULL,0,&event,1,0,&immediate)!=0) return 6;
        close(fd[0]);close(fd[1]);
    }
    close(kq);puts("DISPATCH2 deferred_delete=pass acknowledgement=pass stale_delivery=none cycles=1000");return 0;
}
