/* Own backing only: every received descriptor must have independent lifetime. */
#include <mach/mach.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int fileport_makeport(int,mach_port_t*);
extern int fileport_makefd(mach_port_t);
int main(void) {
 char path[]="/tmp/riot-fileport-lifetime-XXXXXX";int fd=mkstemp(path);
 if(fd<0)return 1;unlink(path);
 const char payload[]="owned-fileport-regression";
 if(write(fd,payload,sizeof(payload))!=sizeof(payload))return 2;
 mach_port_t port=MACH_PORT_NULL;if(fileport_makeport(fd,&port))return 3;
 unsigned completed=0;
 for(unsigned n=0;n<1000;n++) {
  int copy=fileport_makefd(port);if(copy<0)return 4;
  char observed[sizeof(payload)];
  if(pread(copy,observed,sizeof(observed),0)!=sizeof(observed)||memcmp(payload,observed,sizeof(payload)))return 5;
  close(copy);completed++;
 }
 if(mach_port_deallocate(mach_task_self(),port))return 6;
 for(unsigned cycle=0;cycle<100;cycle++) {
  mach_port_t transient=MACH_PORT_NULL;
  if(fileport_makeport(fd,&transient))return 7;
  if(mach_port_mod_refs(mach_task_self(),transient,MACH_PORT_RIGHT_SEND,1))return 8;
  if(mach_port_deallocate(mach_task_self(),transient))return 9;
  int copy=fileport_makefd(transient);if(copy<0)return 10;
  if(mach_port_deallocate(mach_task_self(),transient))return 11;
  char observed[sizeof(payload)];
  if(pread(copy,observed,sizeof(observed),0)!=sizeof(observed)||memcmp(payload,observed,sizeof(payload)))return 12;
  close(copy);
 }
 close(fd);
 printf("FILEPORT_LIFETIME received_closed=%u last_sender_cycles=100 pass=1\n",completed);return 0;
}
