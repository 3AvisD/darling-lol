/* Exercise the production CGL bridge with mocked EGL; no GPU submissions. */
#define glGetError mock_glGetError
#include "../build/darling-src/src/frameworks/OpenGL/OpenGL.c"
#undef glGetError
#include <assert.h>
static int fail_current, gl_calls;
static __thread int bound_api;
static EGLBoolean mock_bind(EGLenum api) { bound_api=api; return EGL_TRUE; }
static EGLContext mock_create(EGLDisplay d,EGLConfig c,EGLContext s,const EGLint*a) {
    return bound_api==EGL_OPENGL_API ? (EGLContext)0x1234 : EGL_NO_CONTEXT;
}
static EGLBoolean mock_current(EGLDisplay d,EGLSurface a,EGLSurface b,EGLContext c) {
    return fail_current ? EGL_FALSE : EGL_TRUE;
}
static EGLBoolean mock_destroy(EGLDisplay d,EGLContext c) { return EGL_TRUE; }
GLenum mock_glGetError(void) { gl_calls++;return GL_NO_ERROR; }
static void *worker(void *arg) {
    CGLContextObj c=0;
    assert(CGLCreateContext(arg,0,&c)==kCGLNoError && bound_api==EGL_OPENGL_API);
    CGLReleaseContext(c);return 0;
}
int main(void) {
    native_egl_loaded=1;display=(EGLDisplay)1;config=(EGLConfig)1;
    p_eglBindAPI=mock_bind;p_eglCreateContext=mock_create;
    p_eglMakeCurrent=mock_current;p_eglDestroyContext=mock_destroy;
    CGLPixelFormatAttribute attrs[]={kCGLPFAOpenGLProfile,0x3200,kCGLPFASampleBuffers,1,kCGLPFASamples,4,0};
    CGLPixelFormatObj pf=0;GLint count=0,value=-1;
    assert(CGLChoosePixelFormat(attrs,&pf,&count)==kCGLNoError);
    assert(pf->attributes[6]==0);
    CGLDescribePixelFormat(pf,0,kCGLPFAOpenGLProfile,&value);assert(value==0x3200);
    CGLDescribePixelFormat(pf,0,kCGLPFASamples,&value);assert(value==4);
    CGLDescribePixelFormat(pf,0,kCGLPFAColorSize,&value);assert(value==0);
    pthread_t thread;assert(!pthread_create(&thread,0,worker,pf));pthread_join(thread,0);
    CGLContextObj defaults=0;
    assert(CGLCreateContext(NULL,NULL,&defaults)==kCGLNoError && defaults);
    CGLReleaseContext(defaults);
    CGLContextObj first=0,second=0;
    assert(CGLCreateContext(pf,0,&first)==kCGLNoError);
    assert(CGLCreateContext(pf,0,&second)==kCGLNoError);
    assert(CGLSetCurrentContext(first)==kCGLNoError);
    struct { GLint screen; unsigned guard; } out={-1,0x11223344};
    assert(CGLGetVirtualScreen(first,&out.screen)==kCGLNoError && out.screen==0 && out.guard==0x11223344);
    assert(CGLSetVirtualScreen(first,1)==kCGLBadValue);
    assert(CGLDescribePixelFormat(pf,0,kCGLPFAVirtualScreenCount,&value)==kCGLNoError && value==1);
    CGLRendererInfoObj info=(void*)1;out.screen=-1;
    assert(CGLQueryRendererInfo(1,&info,&out.screen)==kCGLBadRendererInfo && !info && !out.screen && out.guard==0x11223344);
    fail_current=1;
    assert(CGLSetCurrentContext(second)==kCGLBadState && CGLGetCurrentContext()==first);
    assert(CGLSetCurrentContext(0)==kCGLBadState && CGLGetCurrentContext()==first);
    int dimensions[]={8,8,4,32};const void *keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,kIOSurfaceBytesPerRow};
    const void *values[4];for(int i=0;i<4;i++)values[i]=CFNumberCreate(0,kCFNumberIntType,&dimensions[i]);
    CFDictionaryRef options=CFDictionaryCreate(0,keys,values,4,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    IOSurfaceRef surface=IOSurfaceCreate(options);assert(surface);
    assert(CGLTexImageIOSurface2D(second,GL_TEXTURE_2D,GL_RGBA,8,8,GL_RGBA,GL_UNSIGNED_BYTE,surface,0)==kCGLBadState);
    ioSurfaceBindingCount=1;
    ioSurfaceBindings[0]=(CGLIOSurfaceBinding){IOSurfaceGetID(surface),second,GL_TEXTURE_2D,GL_RGBA,GL_UNSIGNED_BYTE,1,0};
    assert(CGLReadbackIOSurface(surface)==kCGLBadState && !gl_calls && CGLGetCurrentContext()==first);
    fail_current=0;CGLSetCurrentContext(0);CGLReleaseContext(first);CGLReleaseContext(second);
    assert(CGLReadbackIOSurface(surface)==kCGLBadValue && !gl_calls);
    CFRelease(surface);CFRelease(options);for(int i=0;i<4;i++)CFRelease(values[i]);CGLReleasePixelFormat(pf);
    printf("CGL_CHECK terminated=1 profile=1 worker_api=1 failed_bind_preserved=1 rejected_upload=1 rejected_readback=1 destroyed_binding_cleared=1 gl_calls=%d\n",gl_calls);
    return 0;
}
