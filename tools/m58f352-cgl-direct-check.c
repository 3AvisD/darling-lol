#define GL_GLEXT_PROTOTYPES 1
#include <OpenGL/OpenGL.h>
#include <OpenGL/glext.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurface.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
extern long __lolsys_syscall(long,long,long,long,long,long,int);
static int (*present)(IOSurfaceRef);
int main(int argc,char **argv){
 unsigned rate=argc==2?(unsigned)strtoul(argv[1],NULL,10):60;
 if(!rate || rate>240)return 2;
 __lolsys_syscall(0x59616d61,-1,0,0,0,0,157);
 puts("DIRECT_STAGE main");fflush(stdout);
 CGLPixelFormatAttribute attrs[]={kCGLPFAOpenGLProfile,0x3200,kCGLPFAColorSize,32,kCGLPFAAlphaSize,8,0};
 CGLPixelFormatObj pf=0;CGLContextObj ctx=0;GLint screens;
 assert(!CGLChoosePixelFormat(attrs,&pf,&screens));assert(!CGLCreateContext(pf,0,&ctx));assert(!CGLSetCurrentContext(ctx));
 present=(void*)dlsym(RTLD_DEFAULT,"CGLPresentIOSurface");assert(present);puts("DIRECT_STAGE context");fflush(stdout);
 int dims[]={337,339,4,337*4};const void*keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,kIOSurfaceBytesPerRow};const void*values[4];
 for(int i=0;i<4;i++)values[i]=CFNumberCreate(0,kCFNumberIntType,&dims[i]);
 CFDictionaryRef options=CFDictionaryCreate(0,keys,values,4,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);IOSurfaceRef s=IOSurfaceCreate(options);assert(s);
 puts("DIRECT_STAGE surface");fflush(stdout);
 GLuint tex,fbo;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
 assert(!CGLTexImageIOSurface2D(ctx,GL_TEXTURE_2D,GL_RGBA8,337,339,GL_RGBA,GL_UNSIGNED_BYTE,s,0));
 glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
 puts("DIRECT_STAGE framebuffer");fflush(stdout);
 IOSurfaceRef other=NULL;unsigned *targetState=NULL;long targetMapping=-1;
 if(getenv("RCLOL_GPU_DIRECT_OTHER_WINDOW_TEST")) {
  int otherWidth=338;CFNumberRef otherNumber=CFNumberCreate(0,kCFNumberIntType,&otherWidth);
  CFMutableDictionaryRef otherOptions=CFDictionaryCreateMutableCopy(0,0,options);
  CFDictionarySetValue(otherOptions,kIOSurfaceWidth,otherNumber);
  int otherStride=338*4;CFNumberRef strideNumber=CFNumberCreate(0,kCFNumberIntType,&otherStride);
  CFDictionarySetValue(otherOptions,kIOSurfaceBytesPerRow,strideNumber);
  other=IOSurfaceCreate(otherOptions);assert(other);
  GLuint otherTexture;glGenTextures(1,&otherTexture);glBindTexture(GL_TEXTURE_2D,otherTexture);
  assert(!CGLTexImageIOSurface2D(ctx,GL_TEXTURE_2D,GL_RGBA8,338,339,GL_RGBA,GL_UNSIGNED_BYTE,other,0));
  CFRelease(otherOptions);CFRelease(otherNumber);CFRelease(strideNumber);
  char path[128];snprintf(path,sizeof(path),"/dev/shm/darling-ca-gpu-target-%u-337-339",getuid());
  int fd=(int)__lolsys_syscall((long)path,2,0,0,0,0,2);assert(fd>=0);
  targetMapping=__lolsys_syscall(0,64,1,1,fd,0,9);__lolsys_syscall(fd,0,0,0,0,0,3);
  assert(targetMapping>0 || targetMapping < -4095);targetState=(unsigned*)targetMapping;
 }
 sleep(2);
 unsigned success=0;struct {long sec,nsec;} deadline;assert(__lolsys_syscall(1,(long)&deadline,0,0,0,0,228)==0);
 for(unsigned n=0;n<360;n++){
  glClearColor((n%256)/255.f,100/255.f,200/255.f,1);glClear(GL_COLOR_BUFFER_BIT);
  if(n==10){unsigned char rgba[4];glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);printf("DIRECT_SOURCE rgba=%u,%u,%u,%u error=%u\n",rgba[0],rgba[1],rgba[2],rgba[3],glGetError());fflush(stdout);}
  success+=present(s)!=0;
  if(other && n>2) {
   assert(!present(other));
   if(!__atomic_load_n(&targetState[4],__ATOMIC_ACQUIRE)) {
    puts("DIRECT_OTHER_WINDOW active_presenter_preserved=0");fflush(stdout);return 3;
   }
  }
  GLint read=0,draw=0;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);assert(read==fbo&&draw==fbo&&CGLGetCurrentContext()==ctx);
  deadline.nsec+=1000000000/rate;if(deadline.nsec>=1000000000){deadline.nsec-=1000000000;++deadline.sec;}
  if(!getenv("RCLOL_GPU_DIRECT_PACE")){long wait;do{wait=__lolsys_syscall(1,1,(long)&deadline,0,0,0,230);}while(wait==-4);assert(wait==0);}
 }
 printf("DIRECT_PRESENT successful=%u requested=360 gl_state_restored=1 error=%u\n",success,glGetError());fflush(stdout);
 if(other){puts("DIRECT_OTHER_WINDOW active_presenter_preserved=1 fallback_frames=357");CFRelease(other);__lolsys_syscall(targetMapping,64,0,0,0,0,11);}
 CGLSetCurrentContext(0);CGLReleaseContext(ctx);CGLReleasePixelFormat(pf);CFRelease(s);CFRelease(options);for(int i=0;i<4;i++)CFRelease(values[i]);return success==360?0:1;
}
