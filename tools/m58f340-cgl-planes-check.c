#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurface.h>
#include <OpenGL/OpenGL.h>
#include <mach/mach.h>
#include <assert.h>
#include <dlfcn.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
extern CGLError CGLReadbackIOSurface(IOSurfaceRef);
static CFDictionaryRef plane(int w,int h,int element,int row,int offset) {
 const void *keys[]={kIOSurfacePlaneWidth,kIOSurfacePlaneHeight,kIOSurfacePlaneBytesPerElement,kIOSurfacePlaneBytesPerRow,kIOSurfacePlaneOffset};
 int n[]={w,h,element,row,offset};const void *values[5];
 for(int i=0;i<5;i++)values[i]=CFNumberCreate(0,kCFNumberIntType,n+i);
 CFDictionaryRef d=CFDictionaryCreate(0,keys,values,5,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 for(int i=0;i<5;i++)CFRelease(values[i]);return d;
}
int main(void) {
 /* Chromium computes every NV12 plane offset through the alignment API. */
 int videoWidth=1920,videoHeight=1080,videoFormat=0x34323076;
 int videoRow=IOSurfaceAlignProperty(kIOSurfacePlaneBytesPerRow,videoWidth);
 int chromaOffset=IOSurfaceAlignProperty(kIOSurfacePlaneOffset,
     IOSurfaceAlignProperty(kIOSurfacePlaneSize,videoRow*videoHeight));
 int videoAllocation=IOSurfaceAlignProperty(kIOSurfaceAllocSize,
     chromaOffset+IOSurfaceAlignProperty(kIOSurfacePlaneSize,videoRow*(videoHeight/2)));
 assert(videoRow>=videoWidth && chromaOffset>=videoRow*videoHeight && videoAllocation>chromaOffset);
 CFDictionaryRef vp[]={plane(videoWidth,videoHeight,1,videoRow,0),plane(videoWidth/2,videoHeight/2,2,videoRow,chromaOffset)};
 CFArrayRef vl=CFArrayCreate(0,(const void**)vp,2,&kCFTypeArrayCallBacks);
 const void *vk[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfacePixelFormat,kIOSurfacePlaneInfo,kIOSurfaceAllocSize};
 const void *vv[]={CFNumberCreate(0,kCFNumberIntType,&videoWidth),CFNumberCreate(0,kCFNumberIntType,&videoHeight),CFNumberCreate(0,kCFNumberIntType,&videoFormat),vl,CFNumberCreate(0,kCFNumberIntType,&videoAllocation)};
 CFDictionaryRef vo=CFDictionaryCreate(0,vk,vv,5,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 IOSurfaceRef video=IOSurfaceCreate(vo);assert(video && IOSurfaceGetPlaneCount(video)==2 && IOSurfaceGetAllocSize(video)==videoAllocation);
 assert((char*)IOSurfaceGetBaseAddressOfPlane(video,1)-(char*)IOSurfaceGetBaseAddressOfPlane(video,0)==chromaOffset);
 printf("CHROMIUM_NV12_ALLOCATION width=1920 height=1080 row=%d chroma_offset=%d allocation=%d pass=1\n",videoRow,chromaOffset,videoAllocation);
 CFRelease(video);CFRelease(vo);CFRelease(vl);CFRelease(vp[0]);CFRelease(vp[1]);for(int i=0;i<5;i++)if(i!=3)CFRelease(vv[i]);
 CFDictionaryRef ps[]={plane(16,16,1,16,0),plane(8,8,2,16,256)};
 CFArrayRef list=CFArrayCreate(0,(const void**)ps,2,&kCFTypeArrayCallBacks);
 int width=16,height=16,fourcc=0x34323076;
 const void *keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfacePixelFormat,kIOSurfacePlaneInfo};
 const void *values[]={CFNumberCreate(0,kCFNumberIntType,&width),CFNumberCreate(0,kCFNumberIntType,&height),CFNumberCreate(0,kCFNumberIntType,&fourcc),list};
 CFDictionaryRef options=CFDictionaryCreate(0,keys,values,4,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 IOSurfaceRef s=IOSurfaceCreate(options);assert(s && IOSurfaceGetPlaneCount(s)==2 && IOSurfaceGetAllocSize(s)==384);
 /* A decoder surface must never overwrite the native packed-pixel frame slot. */
 void (*publish)(IOSurfaceRef)=(void*)dlsym(RTLD_DEFAULT,"DarlingCAPublishIOSurfacePixels");assert(publish);
 char framePath[128];snprintf(framePath,sizeof(framePath),"/dev/shm/darling-ca-frame-%u-%d",getuid(),getpid());
 publish(s);assert(access(framePath,F_OK)==-1 && errno==ENOENT);
 unsigned char y[256],uv[128];memset(y,92,sizeof(y));for(int i=0;i<128;i+=2){uv[i]=54;uv[i+1]=183;}
 memcpy(IOSurfaceGetBaseAddressOfPlane(s,0),y,256);memcpy(IOSurfaceGetBaseAddressOfPlane(s,1),uv,128);
 mach_port_t port=IOSurfaceCreateMachPort(s);assert(port);
 IOSurfaceRef alias=IOSurfaceLookupFromMachPort(port);assert(alias && IOSurfaceGetPlaneCount(alias)==2 && IOSurfaceGetWidthOfPlane(alias,1)==8 && IOSurfaceGetBytesPerElementOfPlane(alias,1)==2);
 assert(!memcmp(IOSurfaceGetBaseAddressOfPlane(alias,0),y,256) && !memcmp(IOSurfaceGetBaseAddressOfPlane(alias,1),uv,128));
 CGLPixelFormatAttribute attrs[]={kCGLPFAOpenGLProfile,0x3200,0};CGLPixelFormatObj pf=0;GLint screens;CGLContextObj c=0;
 assert(CGLChoosePixelFormat(attrs,&pf,&screens)==0 && CGLCreateContext(pf,0,&c)==0 && CGLSetCurrentContext(c)==0);
 GLuint textures[2];glGenTextures(2,textures);
 glBindTexture(GL_TEXTURE_2D,textures[0]);assert(CGLTexImageIOSurface2D(c,GL_TEXTURE_2D,0x8229,16,16,0x1903,GL_UNSIGNED_BYTE,s,0)==0);
 unsigned char outY[256];glGetTexImage(GL_TEXTURE_2D,0,0x1903,GL_UNSIGNED_BYTE,outY);assert(!memcmp(y,outY,256));
 glBindTexture(GL_TEXTURE_2D,textures[1]);assert(CGLTexImageIOSurface2D(c,GL_TEXTURE_2D,0x822b,8,8,0x8227,GL_UNSIGNED_BYTE,s,1)==0);
 unsigned char outUV[128];glGetTexImage(GL_TEXTURE_2D,0,0x8227,GL_UNSIGNED_BYTE,outUV);assert(!memcmp(uv,outUV,128));
 /* Rebinding a recycled plane must upload its new pixels without reallocating. */
 memset(uv,123,128);memcpy(IOSurfaceGetBaseAddressOfPlane(s,1),uv,128);assert(CGLTexImageIOSurface2D(c,GL_TEXTURE_2D,0x822b,8,8,0x8227,GL_UNSIGNED_BYTE,s,1)==0);
 glGetTexImage(GL_TEXTURE_2D,0,0x8227,GL_UNSIGNED_BYTE,outUV);assert(!memcmp(uv,outUV,128));
 memset(IOSurfaceGetBaseAddressOfPlane(s,1),0,128);assert(CGLReadbackIOSurface(s)==0 && !memcmp(IOSurfaceGetBaseAddressOfPlane(alias,1),uv,128));
 assert(CGLTexImageIOSurface2D(c,GL_TEXTURE_2D,0x822b,8,8,0x8227,GL_UNSIGNED_BYTE,s,2)==kCGLBadValue);
 glDeleteTextures(2,textures);CGLSetCurrentContext(0);CGLReleaseContext(c);CGLReleasePixelFormat(pf);
 CFRelease(alias);CFRelease(s);mach_port_deallocate(mach_task_self(),port);CFRelease(options);for(int i=0;i<3;i++)CFRelease(values[i]);CFRelease(list);CFRelease(ps[0]);CFRelease(ps[1]);
 puts("CGL_PLANES count=2 dimensions=pass transport=pass luma=pass chroma=pass invalid_plane_rejected=pass");return 0;
}
