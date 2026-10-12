#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurface.h>
#include <mach/mach.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
struct ConcurrentImport {mach_port_t port;IOSurfaceRef expected;};
static void *lookupConcurrently(void *arg) {
 struct ConcurrentImport *test=arg;
 for(unsigned i=0;i<100;i++) {
  IOSurfaceRef surface=IOSurfaceLookupFromMachPort(test->port);
  if(surface!=test->expected)return (void*)1;
  CFRelease(surface);
 }
 return NULL;
}
extern long __lolsys_syscall(long,long,long,long,long,long,int);
static int descriptorCount(void) {
 int fd=__lolsys_syscall((long)"/proc/self/fd",0,0,0,0,0,2);if(fd<0)return -1;
 char data[8192];int count=0;long n;
 while((n=__lolsys_syscall(fd,(long)data,sizeof(data),0,0,0,217))>0) {
  for(long at=0;at<n;) {
   unsigned short length;memcpy(&length,data+at+16,2);
   if(length<20||at+length>n){count=-1000;break;}
   const char*name=data+at+19;if(name[0]!='.')count++;at+=length;
  }
 }
 __lolsys_syscall(fd,0,0,0,0,0,3);return count;
}
int main(void) {
 int dims[]={8,8,4,32};const void *keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,kIOSurfaceBytesPerRow};
 const void *values[4];for(int i=0;i<4;i++)values[i]=CFNumberCreate(0,kCFNumberIntType,&dims[i]);
 CFDictionaryRef options=CFDictionaryCreate(0,keys,values,4,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 IOSurfaceID firstID=0,ids[100];int before=descriptorCount();assert(before>=0);
 for(int n=0;n<100;n++) {
  IOSurfaceRef original=IOSurfaceCreate(options);assert(original);
  memset(IOSurfaceGetBaseAddress(original),n,256);
  IOSurfaceID sid=IOSurfaceGetID(original);ids[n]=sid;if(!n)firstID=sid;mach_port_t port=IOSurfaceCreateMachPort(original);assert(port);
  IOSurfaceRef same=IOSurfaceLookup(sid);assert(same==original);CFRelease(same);
  CFRelease(original);
  assert(!IOSurfaceLookup(sid));
  IOSurfaceRef imported=IOSurfaceLookupFromMachPort(port);assert(imported && IOSurfaceGetID(imported)==sid);
  IOSurfaceRef repeated=IOSurfaceLookupFromMachPort(port);assert(repeated==imported);
  CFRelease(repeated);
  if(!n) {
   struct ConcurrentImport test={port,imported};pthread_t threads[4];
   for(unsigned i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,lookupConcurrently,&test));
   for(unsigned i=0;i<4;i++){void *result;assert(!pthread_join(threads[i],&result)&&!result);}
  }
  unsigned char *bytes=IOSurfaceGetBaseAddress(imported);assert(bytes);for(int i=0;i<256;i++)assert(bytes[i]==(unsigned char)n);
  CFRelease(imported);assert(mach_port_deallocate(mach_task_self(),port)==KERN_SUCCESS);
  assert(!IOSurfaceLookupFromMachPort(port));
 }
 CFRelease(options);for(int i=0;i<4;i++)CFRelease(values[i]);
 int after=descriptorCount();assert(after>=0 && after-before<=10);
 printf("IMPORT_CACHE fd_growth=%d bounded=1 aliases_shared=1 concurrent_lookups=400 iterations=100 pass=1\n",after-before);
 printf("SURFACE_ID first=%u\n",firstID);
 printf("SURFACE_IDS");for(int n=0;n<100;n++)printf(" %u",ids[n]);putchar('\n');
 printf("SURFACE_LIFETIME iterations=100 released_id_absent=1 port_survives_release=1 pixels=pass invalid_port_rejected=1\n");return 0;
}
