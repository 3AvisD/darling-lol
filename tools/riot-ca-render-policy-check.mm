#import <Foundation/Foundation.h>
#import <QuartzCore/CALayer.h>
#import <QuartzCore/CALayerContext.h>
#import "CAMetalLayerInternal.h"
#import <IOSurface/IOSurface.h>
#include <stdio.h>
#include <stdlib.h>

@interface CALayerContext (RenderPolicyCheckPrivate)
- (void)prepareMetalLayers:(CALayer*)layer;
@end
static unsigned prepared, renderCalls;
@interface RenderPolicyMetalLayer : CAMetalLayerInternal
@end
@implementation RenderPolicyMetalLayer
- (void)prepareRender { ++prepared; }
@end
@interface RenderPolicySurfaceLayer : CALayer { IOSurfaceRef _probeSurface; }
- (id)initWithSurface:(IOSurfaceRef)surface;
@end
@implementation RenderPolicySurfaceLayer
- (id)initWithSurface:(IOSurfaceRef)surface {
 self=[super init]; _probeSurface=surface; return self;
}
- (id)contents { return (id)_probeSurface; }
@end
@interface RenderPolicyContext : CALayerContext
- (id)initWithRoot:(CALayer*)root;
@end
@implementation RenderPolicyContext
- (id)initWithRoot:(CALayer*)root {
 self=[super initWithFrame:CGRectMake(0,0,16,16)];
 _layer=[root retain]; _cglWindow=(void*)1; return self;
}
- (void)renderLayer:(CALayer*)layer { ++renderCalls; [self prepareMetalLayers:layer]; }
@end
int main() {
 // No NSApplication, window, graphics context, GPU work, or published IOSurface.
 // Objects intentionally live until process exit; the sentinel is never passed to CGL.
 [NSAutoreleasePool new];
 unsetenv("RCLOL_GPU_DIRECT_PRESENT");
 int dimensions[]={16,16,4,64};
 const void* keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,kIOSurfaceBytesPerRow};
 const void* values[4];
 for(unsigned i=0;i<4;++i) values[i]=CFNumberCreate(NULL,kCFNumberIntType,&dimensions[i]);
 CFDictionaryRef properties=CFDictionaryCreate(NULL,keys,values,4,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 IOSurfaceRef surface=IOSurfaceCreate(properties);
 if(!surface) return 2;
 CALayer* root=[CALayer new];
 CALayer* container=[CALayer new];
 RenderPolicyMetalLayer* metal=[RenderPolicyMetalLayer new];
 [container addSublayer:metal];
 [root addSublayer:container];
 RenderPolicyContext* context=[[RenderPolicyContext alloc] initWithRoot:root];
 setenv("DARLING_CA_SKIP_LOCAL_IOSURFACE_RENDER","1",1);
 for(unsigned i=0;i<3;++i) [context render];
 printf("metal_only: prepared=%u expected=3\n",prepared);
 if(prepared!=3) return 3;
 RenderPolicySurfaceLayer* surfaceLayer=[[RenderPolicySurfaceLayer alloc] initWithSurface:surface];
 [root addSublayer:surfaceLayer];
 prepared=0;
 for(unsigned i=0;i<3;++i) [context render];
 printf("mixed_tree_shortcut_enabled: prepared=%u expected=3\n",prepared);
 if(prepared!=3) return 4;
 [root setSublayers:@[surfaceLayer,container]];
 prepared=0;
 for(unsigned i=0;i<3;++i) [context render];
 printf("mixed_tree_surface_first: prepared=%u expected=3\n",prepared);
 if(prepared!=3) return 7;
 unsetenv("DARLING_CA_SKIP_LOCAL_IOSURFACE_RENDER");
 prepared=0;
 for(unsigned i=0;i<3;++i) [context render];
 printf("mixed_tree_shortcut_disabled: prepared=%u expected=3\n",prepared);
 if(prepared!=3) return 5;
 setenv("DARLING_CA_SKIP_LOCAL_IOSURFACE_RENDER","1",1);
 RenderPolicyContext* surfaceOnly=[[RenderPolicyContext alloc] initWithRoot:surfaceLayer];
 renderCalls=0;
 [surfaceOnly render];
 printf("surface_only_shortcut_enabled: renderCalls=%u expected=0\n",renderCalls);
 if(renderCalls!=0) return 6;
 // Partial IOSurfaces bypass local rendering only in CPU-readback mode.
 int partialDimensions[]={8,8,4,32};
 for(unsigned i=0;i<4;++i) values[i]=CFNumberCreate(NULL,kCFNumberIntType,&partialDimensions[i]);
 CFDictionaryRef partialProperties=CFDictionaryCreate(NULL,keys,values,4,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 IOSurfaceRef partial=IOSurfaceCreate(partialProperties);
 if(!partial) return 8;
 RenderPolicySurfaceLayer* partialLayer=[[RenderPolicySurfaceLayer alloc] initWithSurface:partial];
 RenderPolicyContext* partialContext=[[RenderPolicyContext alloc] initWithRoot:partialLayer];
 renderCalls=0;
 [partialContext render];
 if(renderCalls!=0) return 9;
 setenv("RCLOL_GPU_DIRECT_PRESENT","1",1);
 [partialContext render];
 if(renderCalls!=1) return 10;
 unsetenv("DARLING_CA_SKIP_LOCAL_IOSURFACE_RENDER");
 unsetenv("RCLOL_GPU_DIRECT_PRESENT");
 [partialContext render];
 if(renderCalls!=2) return 11;
 printf("PASS: mixed-tree Metal preparation survives IOSurface shortcut\n");
 return 0;
}
