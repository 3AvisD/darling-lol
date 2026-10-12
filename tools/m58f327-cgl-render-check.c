/* Tiny native CGL/IOSurface round-trip, with a rejected cross-thread bind. */
#include <OpenGL/OpenGL.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurface.h>
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern CGLError CGLReadbackIOSurface(IOSurfaceRef);
static CGLContextObj owner;
static CGLPixelFormatObj format;
static IOSurfaceRef surface;
static void *worker(void *unused) {
    CGLContextObj c=0;assert(CGLCreateContext(format,0,&c)==kCGLNoError);
    assert(CGLSetCurrentContext(c)==kCGLNoError);
    assert(glGetString(GL_VERSION));
    assert(CGLSetCurrentContext(owner)==kCGLBadState && CGLGetCurrentContext()==c);
    assert(CGLReadbackIOSurface(surface)==kCGLBadState && CGLGetCurrentContext()==c);
    assert(CGLSetCurrentContext(0)==kCGLNoError);CGLReleaseContext(c);return 0;
}
int main(void) {
    CGLPixelFormatAttribute attributes[]={kCGLPFAColorSize,32,kCGLPFAAlphaSize,8,0};
    CGLPixelFormatAttribute core[]={kCGLPFAOpenGLProfile,0x3200,kCGLPFAColorSize,32,kCGLPFAAlphaSize,8,0};GLint screens=0;
    assert(CGLChoosePixelFormat(getenv("M327_CGL_CORE")?core:attributes,&format,&screens)==kCGLNoError);
    assert(CGLCreateContext(format,0,&owner)==kCGLNoError);
    assert(CGLSetCurrentContext(owner)==kCGLNoError);
    const char *renderer=(const char*)glGetString(GL_RENDERER);assert(renderer);
    printf("CGL_RENDER renderer=%s\n",renderer);fflush(stdout);
    int dims[]={8,8,4,32};const void *keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,kIOSurfaceBytesPerRow};
    const void *values[4];for(int i=0;i<4;i++)values[i]=CFNumberCreate(0,kCFNumberIntType,&dims[i]);
    CFDictionaryRef options=CFDictionaryCreate(0,keys,values,4,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    surface=IOSurfaceCreate(options);assert(surface);
    unsigned char expected[256];for(int i=0;i<256;i+=4){expected[i]=33;expected[i+1]=55;expected[i+2]=77;expected[i+3]=255;}
    memcpy(IOSurfaceGetBaseAddress(surface),expected,sizeof(expected));
    GLuint texture;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    assert(CGLTexImageIOSurface2D(owner,GL_TEXTURE_2D,GL_RGBA,8,8,GL_RGBA,GL_UNSIGNED_BYTE,surface,0)==kCGLNoError);
    memset(IOSurfaceGetBaseAddress(surface),0,256);
    assert(CGLReadbackIOSurface(surface)==kCGLNoError);
    assert(!memcmp(IOSurfaceGetBaseAddress(surface),expected,sizeof(expected)));
    /* More than the old 256-entry cache, while the display binding stays live. */
    IOSurfaceRef extra[300];GLuint textures[300];glGenTextures(300,textures);
    for(int i=0;i<300;i++) {
        extra[i]=IOSurfaceCreate(options);assert(extra[i]);
        glBindTexture(GL_TEXTURE_2D,textures[i]);
        assert(CGLTexImageIOSurface2D(owner,GL_TEXTURE_2D,GL_RGBA,8,8,GL_RGBA,GL_UNSIGNED_BYTE,extra[i],0)==kCGLNoError);
    }
    memset(IOSurfaceGetBaseAddress(surface),0,256);
    assert(CGLReadbackIOSurface(surface)==kCGLNoError);
    assert(!memcmp(IOSurfaceGetBaseAddress(surface),expected,sizeof(expected)));
    for(int i=0;i<300;i++)CFRelease(extra[i]);glDeleteTextures(300,textures);
    pthread_t thread;assert(!pthread_create(&thread,0,worker,0));pthread_join(thread,0);
    glDeleteTextures(1,&texture);CGLSetCurrentContext(0);CGLReleaseContext(owner);
    assert(CGLReadbackIOSurface(surface)==kCGLBadValue);
    CGLReleasePixelFormat(format);CFRelease(surface);CFRelease(options);for(int i=0;i<4;i++)CFRelease(values[i]);
    printf("CGL_RENDER pixels=pass worker_api=pass cross_thread_rejected=pass destroyed_binding=pass live_bindings_over256=pass\n");return 0;
}
