#include <CoreVideo/CVDisplayLink.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <mach/mach_time.h>
#include <stdlib.h>
static CVReturn tick(CVDisplayLinkRef l,const CVTimeStamp *now,const CVTimeStamp *out,CVOptionFlags in,CVOptionFlags *flags,void *info) {
 uint64_t t=now->hostTime;
 char line[80];int n=snprintf(line,sizeof(line),"%llu\n",(unsigned long long)t);
 int fd=open("/Volumes/SystemRoot/tmp/m341-clock-samples",O_CREAT|O_WRONLY|O_APPEND,0600);
 if(fd>=0){write(fd,line,n);close(fd);}
 if(getenv("M342_SELF_STOP")) assert(CVDisplayLinkStop(l)==0);
 return kCVReturnSuccess;
}
int main(void) {
 CVDisplayLinkRef link=0;assert(CVDisplayLinkCreateWithActiveCGDisplays(&link)==0);
 assert(!CVDisplayLinkIsRunning(link));
 CVDisplayLinkRetain(link);assert(!CVDisplayLinkIsRunning(link));CVDisplayLinkRelease(link);
 assert(CVDisplayLinkSetOutputCallback(link,tick,0)==0 && CVDisplayLinkStart(link)==0);
 sleep(4);CVDisplayLinkStop(link);
 CVDisplayLinkRetain(link);assert(!CVDisplayLinkIsRunning(link));CVDisplayLinkRelease(link);
 int fd=open("/Volumes/SystemRoot/tmp/m341-clock-samples",O_WRONLY|O_APPEND,0600);if(fd>=0){write(fd,"STOP\n",5);close(fd);}
 sleep(1);CVDisplayLinkRelease(link);puts("DISPLAY_CLOCK completed");return 0;
}
