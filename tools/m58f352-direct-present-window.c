/* Public synthetic window for the native GPU presentation probe. */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
extern void *DarlingXShmCreate(Display*,Visual*,unsigned,unsigned,unsigned);
extern int DarlingXShmStart(void*,Drawable,GC,const char*);
extern void DarlingXShmDestroy(void*);
int main(void){
 Display*d=XOpenDisplay(NULL);if(!d)return 1;XVisualInfo v;
 if(!XMatchVisualInfo(d,DefaultScreen(d),32,TrueColor,&v))return 2;
 for(unsigned j=0;j<37;++j)XCreateSimpleWindow(d,DefaultRootWindow(d),0,0,1,1,0,0,0);
 Colormap cmap=XCreateColormap(d,DefaultRootWindow(d),v.visual,AllocNone);
 XSetWindowAttributes a={.colormap=cmap,.border_pixel=0,.background_pixel=0,.override_redirect=True};
 Window w=XCreateWindow(d,DefaultRootWindow(d),20,20,1536,864,0,32,InputOutput,v.visual,CWColormap|CWBorderPixel|CWBackPixel|CWOverrideRedirect,&a);
 XStoreName(d,w,"Synthetic GPU frame probe");XMapRaised(d,w);XSync(d,False);
 int fd=open("/tmp/m352-synthetic-slot",O_CREAT|O_RDWR,0600);size_t size=32+1536*864*4;
 if(fd<0||ftruncate(fd,size))return 3;
 uint32_t*h=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);close(fd);
 h[0]=0x43414652;h[1]=2;h[2]=1536;h[3]=864;h[4]=1536*4;h[5]=0;((uint64_t*)h)[3]=1536*864*4;
 void*f=DarlingXShmCreate(d,v.visual,32,1536,864);if(!f||!DarlingXShmStart(f,w,DefaultGC(d,DefaultScreen(d)),"/tmp/m352-synthetic-slot"))return 4;
 printf("%lu\n",w);fflush(stdout);sleep(12);DarlingXShmDestroy(f);munmap(h,size);XDestroyWindow(d,w);XFreeColormap(d,cmap);XCloseDisplay(d);return 0;
}
