#include <sys/event.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
extern long __lolsys_syscall(long,long,long,long,long,long,int);
static unsigned descriptors(void) {
 int fd=__lolsys_syscall((long)"/proc/self/fd",0,0,0,0,0,2);
 assert(fd>=0);unsigned count=0;char data[32768];long n;
 while((n=__lolsys_syscall(fd,(long)data,sizeof(data),0,0,0,217))>0){
  for(long i=0;i<n;){unsigned short size=*(unsigned short*)(data+i+16);
   assert(size>=20);if(data[i+19]>='0'&&data[i+19]<='9')++count;i+=size;
  }
 }
 __lolsys_syscall(fd,0,0,0,0,0,3);return count;
}
int main(void) {
 mach_port_t port;assert(mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&port)==KERN_SUCCESS);
 unsigned before=descriptors();
 for(unsigned i=0;i<1000;++i){
  int q=kqueue();assert(q>=0);
  struct kevent64_s event={0};event.ident=port;event.filter=EVFILT_MACHPORT;event.flags=EV_ADD;
  assert(kevent64(q,&event,1,0,0,0,0)==0);assert(close(q)==0);
 }
 unsigned after=descriptors();
 printf("KQUEUE_LIFETIME cycles=1000 before=%u after=%u growth=%d\n",before,after,(int)after-(int)before);
 assert(after<=before+2);
 for(unsigned i=0;i<100;++i){
  int fds[2];assert(pipe(fds)==0);int q=kqueue();assert(q>=0);
  struct kevent64_s events[3]={{0}};
  events[0].ident=fds[0];events[0].filter=EVFILT_READ;events[0].flags=EV_ADD;
  events[1].ident=1;events[1].filter=EVFILT_USER;events[1].flags=EV_ADD;
  events[2].ident=2;events[2].filter=EVFILT_TIMER;events[2].flags=EV_ADD;events[2].data=1000;
  assert(kevent64(q,events,3,0,0,0,0)==0);assert(close(q)==0);
  char byte='x',got=0;assert(write(fds[1],&byte,1)==1);assert(read(fds[0],&got,1)==1&&got==byte);
  assert(close(fds[0])==0&&close(fds[1])==0);
 }
 unsigned mixed=descriptors();
 printf("KQUEUE_FILTER_LIFETIME cycles=100 after=%u growth=%d caller_pipe_preserved=1\n",mixed,(int)mixed-(int)after);
 mach_port_destroy(mach_task_self(),port);return mixed<=after+2?0:1;
}
