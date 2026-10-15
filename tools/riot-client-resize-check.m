#import <AppKit/AppKit.h>
#import <QuartzCore/CALayer.h>
#import <QuartzCore/CATransaction.h>
#import <IOSurface/IOSurface.h>
#import <X11/Xlib.h>
#import <X11/Xutil.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>
#include <stdio.h>
#include <dlfcn.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>

@interface NSObject (NativeWindowID)
- (unsigned long)windowHandle;
@end
static void pump(double seconds) {
 NSDate *end=[NSDate dateWithTimeIntervalSinceNow:seconds];
 while([end timeIntervalSinceNow]>0) {
  NSEvent *e=[NSApp nextEventMatchingMask:NSAnyEventMask untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES];
  if(e)[NSApp sendEvent:e];
  [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.005]];
 }
}
static IOSurfaceRef surface(int width,int height,int generation) {
 int dims[]={width,height,4,width*4,0x42475241};
 const void *keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,kIOSurfaceBytesPerRow,kIOSurfacePixelFormat},*values[5];
 for(int i=0;i<5;i++)values[i]=CFNumberCreate(NULL,kCFNumberIntType,&dims[i]);
 CFDictionaryRef options=CFDictionaryCreate(NULL,keys,values,5,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 IOSurfaceRef s=IOSurfaceCreate(options);CFRelease(options);for(int i=0;i<5;i++)CFRelease(values[i]);
 if(!s)return NULL;
 unsigned char *pixels=IOSurfaceGetBaseAddress(s);size_t stride=IOSurfaceGetBytesPerRow(s);
 for(int y=0;y<height;y++)for(int x=0;x<width;x++){unsigned char *p=pixels+y*stride+x*4;p[0]=x<width/2?0:255;p[1]=y<height/2?255:0;p[2]=40+generation*40;p[3]=255;}
 return s;
}
/* Exercise the transport protocol with two owned fixture publishers. The
 * software owner changes to a popup while readback supplies the resized main
 * window, as happens when a browser GPU process serves multiple windows. */
typedef struct {
 uint32_t magic, sequence, width, height, stride, format;
 uint64_t size;
} FixtureFrame;
static BOOL writeFixture(const char *path,int width,int height,int generation,BOOL software) {
 IOSurfaceRef s=surface(width,height,generation);if(!s)return NO;
 size_t stride=IOSurfaceGetBytesPerRow(s),bytes=stride*height;
 int fd=open(path,O_CREAT|O_RDWR,0600);
 BOOL ok=fd>=0 && ftruncate(fd,32+64u*1024u*1024u)==0;
 static uint32_t sequence=0;sequence+=2;
 FixtureFrame head={0x43414652u,sequence|1,width,height,(uint32_t)stride,
     0x42475241u|(software?0x80000000u:0),bytes};
 if(ok)ok=pwrite(fd,&head,sizeof(head),0)==sizeof(head) &&
     pwrite(fd,IOSurfaceGetBaseAddress(s),bytes,sizeof(head))==(ssize_t)bytes;
 head.sequence=sequence;
 if(ok)ok=pwrite(fd,&head,sizeof(head),0)==sizeof(head);
 if(fd>=0)close(fd);CFRelease(s);return ok;
}
static int checkPublisherHandoff(void) {
 char owner[128],readback[128];
 snprintf(owner,sizeof(owner),"/dev/shm/darling-ca-frame-%u-%d",getuid(),getpid());
 snprintf(readback,sizeof(readback),"/dev/shm/darling-ca-frame-%u-%d",getuid(),getpid()+100000000);
 NSWindow *w=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,1280,720)
     styleMask:NSBorderlessWindowMask backing:NSBackingStoreBuffered defer:NO];
 [w setTitle:@"Darling standalone publisher handoff"];
 [w makeKeyAndOrderFront:nil];
 Display *d=XOpenDisplay(NULL);Window xid=[(id)[w platformWindow] windowHandle];
 BOOL pass=d!=NULL;
 int sizes[][2]={{1280,720},{1600,900},{1024,576},{1280,720}};
 for(int n=0;pass && n<4;n++) {
  int width=sizes[n][0],height=sizes[n][1];
  [w setContentSize:NSMakeSize(width,height)];
  if(n==0)pass=writeFixture(owner,width,height,n,YES);
  else pass=writeFixture(owner,262,131,0,YES) &&
      writeFixture(readback,width,height,n,NO);
  BOOL ok=NO;unsigned long pixel=0;
  NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:3.0];
  while(pass && !ok && [deadline timeIntervalSinceNow]>0) {
   pump(.05);XSync(d,False);
   XImage *im=XGetImage(d,xid,0,0,width,height,AllPlanes,ZPixmap);
   if(im){pixel=XGetPixel(im,width-8,height-8)&0xffffff;
    ok=pixel==(((unsigned long)(40+n*40)<<16)|0xff);XDestroyImage(im);}
  }
  printf("PUBLISHER_HANDOFF size=%dx%d bottom_right=%06lx pass=%d\n",width,height,pixel,ok);
  pass &=ok;
 }
 [w orderOut:nil];[w release];if(d)XCloseDisplay(d);
 unlink(owner);unlink(readback);return pass?0:1;
}
int main(int argc, const char **argv) {
 @autoreleasepool {
  [NSApplication sharedApplication];
  if(argc>1 && strcmp(argv[1],"--handoff")==0)return checkPublisherHandoff();
  NSWindow *w=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,1280,720) styleMask:NSBorderlessWindowMask backing:NSBackingStoreBuffered defer:NO];
  [w setTitle:@"Darling standalone resize pixels"];
  NSView *view=[w contentView];[view setWantsLayer:YES];
  [w makeKeyAndOrderFront:nil];pump(.15);
  Display *d=XOpenDisplay(NULL);Window xid=[(id)[w platformWindow] windowHandle];
  void (*publish)(IOSurfaceRef)=dlsym(RTLD_DEFAULT,"DarlingCAPublishIOSurfacePixels");
  if(!d||!publish)return 2;
  BOOL useGPU = argc > 1 && strcmp(argv[1], "--gpu") == 0;
  IOSurfaceRef bootstrap=surface(1280,720,0);
  [view.layer setContents:(id)bootstrap];publish(bootstrap);pump(.5);CFRelease(bootstrap);
  CGLPixelFormatAttribute attrs[]={0};CGLPixelFormatObj format;GLint screens;CGLContextObj gpu;
  GLuint texture=0;
  if(useGPU) {
   if(CGLChoosePixelFormat(attrs,&format,&screens)||CGLCreateContext(format,NULL,&gpu))return 4;
   CGLSetCurrentContext(gpu);glGenTextures(1,&texture);
  }
  BOOL pass=YES;
  int sizes[][2]={{1280,720},{1600,900},{1024,576},{1280,720}};
  for(int n=0;n<4;n++) {
   int width=sizes[n][0],height=sizes[n][1];
   [w setContentSize:NSMakeSize(width,height)];
   IOSurfaceRef s=surface(width,height,n);if(!s)return 3;
   if(useGPU) {
    CGLSetCurrentContext(gpu);glBindTexture(GL_TEXTURE_RECTANGLE,texture);
    if(CGLTexImageIOSurface2D(gpu,GL_TEXTURE_RECTANGLE,GL_RGBA8,width,height,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,s,0))return 5;
   }
   for(int f=0;f<8;f++) {
    if(useGPU)CGLSetCurrentContext(gpu);
    [view.layer setContents:(id)s];
    if(!useGPU)publish(s);
    [CATransaction flush];pump(.1);
   }
   BOOL ok=NO;
   unsigned long pixel=0;
   NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:2.5];
   do {
    XSync(d,False);
    XImage *im=XGetImage(d,xid,0,0,width,height,AllPlanes,ZPixmap);
    if(im) {
     pixel=XGetPixel(im,width-8,height-8)&0xffffff;
     ok=pixel==(((unsigned long)(40+n*40)<<16)|0xff);
     XDestroyImage(im);
    }
    if(!ok)pump(.05);
   } while(!ok && [deadline timeIntervalSinceNow]>0);
   printf("RESIZE_PIXEL mode=%s size=%dx%d view=%.0fx%.0f bottom_right=%06lx pass=%d\n",
       useGPU?"gpu":"software",width,height,view.frame.size.width,view.frame.size.height,pixel,ok);
   pass &=ok;CFRelease(s);
  }
  if(useGPU){CGLSetCurrentContext(gpu);glDeleteTextures(1,&texture);CGLSetCurrentContext(NULL);CGLReleaseContext(gpu);CGLReleasePixelFormat(format);}
  [w orderOut:nil];[w release];XCloseDisplay(d);
  return pass?0:1;
 }
}
