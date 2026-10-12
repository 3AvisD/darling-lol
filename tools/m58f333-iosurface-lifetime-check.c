#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurface.h>
#include <mach/mach.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
 int dims[]={8,8,4,32};const void *keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,kIOSurfaceBytesPerRow};
 const void *values[4];for(int i=0;i<4;i++)values[i]=CFNumberCreate(0,kCFNumberIntType,&dims[i]);
 CFDictionaryRef options=CFDictionaryCreate(0,keys,values,4,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 IOSurfaceID firstID=0,ids[100];
 for(int n=0;n<100;n++) {
  IOSurfaceRef original=IOSurfaceCreate(options);assert(original);
  memset(IOSurfaceGetBaseAddress(original),n,256);
  IOSurfaceID sid=IOSurfaceGetID(original);ids[n]=sid;if(!n)firstID=sid;mach_port_t port=IOSurfaceCreateMachPort(original);assert(port);
  IOSurfaceRef same=IOSurfaceLookup(sid);assert(same==original);CFRelease(same);
  CFRelease(original);
  assert(!IOSurfaceLookup(sid));
  IOSurfaceRef imported=IOSurfaceLookupFromMachPort(port);assert(imported && IOSurfaceGetID(imported)==sid);
  unsigned char *bytes=IOSurfaceGetBaseAddress(imported);assert(bytes);for(int i=0;i<256;i++)assert(bytes[i]==(unsigned char)n);
  CFRelease(imported);assert(mach_port_deallocate(mach_task_self(),port)==KERN_SUCCESS);
  assert(!IOSurfaceLookupFromMachPort(port));
 }
 CFRelease(options);for(int i=0;i<4;i++)CFRelease(values[i]);
 printf("SURFACE_ID first=%u\n",firstID);
 printf("SURFACE_IDS");for(int n=0;n<100;n++)printf(" %u",ids[n]);putchar('\n');
 printf("SURFACE_LIFETIME iterations=100 released_id_absent=1 port_survives_release=1 pixels=pass invalid_port_rejected=1\n");return 0;
}
