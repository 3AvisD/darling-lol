/* Exercise the first floating-point calls through generated ELF GL resolvers. */
#define GL_GLEXT_PROTOTYPES 1
#include <OpenGL/OpenGL.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <dlfcn.h>
int main(void) {
    CGLPixelFormatAttribute attrs[]={kCGLPFAOpenGLProfile,0x3200,0};
    CGLPixelFormatObj pf=0;CGLContextObj ctx=0;GLint screens=0;
    if(CGLChoosePixelFormat(attrs,&pf,&screens) || CGLCreateContext(pf,0,&ctx) || CGLSetCurrentContext(ctx))return 2;
    while(glGetError()!=GL_NO_ERROR){}
    GLuint sampler=0;glGenSamplers(1,&sampler);
    void (*samplerParameterf)(GLuint,GLenum,GLfloat)=(void*)dlsym(RTLD_DEFAULT,"glSamplerParameterf");
    void (*clearColor)(GLfloat,GLfloat,GLfloat,GLfloat)=(void*)dlsym(RTLD_DEFAULT,"glClearColor");
    if(!samplerParameterf || !clearColor)return 2;
    samplerParameterf(sampler,GL_TEXTURE_MIN_FILTER,(GLfloat)GL_LINEAR);
    GLint filter=0;glGetSamplerParameteriv(sampler,GL_TEXTURE_MIN_FILTER,&filter);
    GLenum samplerError=glGetError();
    clearColor(.125f,.25f,.5f,.75f);
    GLfloat rgba[4]={0};glGetFloatv(GL_COLOR_CLEAR_VALUE,rgba);
    GLenum colorError=glGetError();
    int colorOK=rgba[0]==.125f && rgba[1]==.25f && rgba[2]==.5f && rgba[3]==.75f;
    printf("GL_RESOLVER first_sampler_linear=%d first_color_arguments=%d errors=%u,%u\n",filter==GL_LINEAR,colorOK,samplerError,colorError);
    glDeleteSamplers(1,&sampler);CGLSetCurrentContext(0);CGLReleaseContext(ctx);CGLReleasePixelFormat(pf);
    return filter==GL_LINEAR && colorOK && !samplerError && !colorError?0:1;
}
