#include <sys/event.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
int main(void) {
    char path[]="/tmp/riot-vnode-check-XXXXXX";int fd=mkstemp(path),kq=kqueue();
    if(fd<0||kq<0)return 1;
    struct timespec immediate={0,0};
    struct kevent64_s change={.ident=fd,.filter=EVFILT_VNODE,
        .flags=EV_ADD|EV_CLEAR|EV_DISPATCH2,.fflags=NOTE_WRITE|NOTE_EXTEND,.udata=123},event={0};
    if(kevent64(kq,&change,1,NULL,0,0,&immediate))return 2;
    for(unsigned cycle=0;cycle<100;cycle++) {
        int other=open(path,O_RDONLY);if(other<0)return 3;close(other);
        if(kevent64(kq,NULL,0,&event,1,0,&immediate)!=0) return 4;
        if(write(fd,"x",1)!=1)return 5;
        int n=kevent64(kq,NULL,0,&event,1,0,&immediate);
        if(n!=1 || event.filter!=EVFILT_VNODE || !(event.fflags&NOTE_WRITE) || !(event.fflags&NOTE_EXTEND)) {
            printf("VNODE_CLOSE preserved=fail cycle=%u events=%d flags=%x\n",cycle,n,event.fflags);return 6;
        }
        change.flags=EV_ENABLE|EV_UDATA_SPECIFIC|EV_RECEIPT;
        if(kevent64(kq,&change,1,&event,1,0,&immediate)!=1 || event.data) return 7;
    }
    close(kq);close(fd);unlink(path);
    puts("VNODE_CLOSE unrelated_close=ignored write=pass extend=pass rearm=pass cycles=100");return 0;
}
