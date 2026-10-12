#define PRIVATE 1
#include <sys/event.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
static void* check(void* unused) {
    (void)unused;
    struct kevent_qos_s *changes=calloc(512,sizeof(*changes));
    struct kevent_qos_s *events=calloc(512,sizeof(*events));
    assert(changes && events);
    int q=kqueue();assert(q>=0);
    for(unsigned i=0;i<512;i++) {
        changes[i].ident=i+1;changes[i].filter=EVFILT_USER;
        changes[i].flags=EV_ADD|EV_RECEIPT;
    }
    int n=kevent_qos(q,changes,512,events,512,0,0,KEVENT_FLAG_IMMEDIATE);
    printf("KQUEUE_SMALL_STACK stack_bytes=65536 changes=512 receipts=%d\n",n);
    assert(n==512);
    for(int i=0;i<n;i++) assert(events[i].data==0);
    close(q);free(changes);free(events);return 0;
}
int main(void) {
    pthread_attr_t attr;pthread_t thread;
    assert(!pthread_attr_init(&attr));
    assert(!pthread_attr_setstacksize(&attr,65536));
    assert(!pthread_create(&thread,&attr,check,0));
    pthread_attr_destroy(&attr);assert(!pthread_join(thread,0));
    puts("KQUEUE_SMALL_STACK pass=1");return 0;
}
