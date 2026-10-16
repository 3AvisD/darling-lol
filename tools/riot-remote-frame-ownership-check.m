/* No display connection or window is created. Exercise the real backend with a cached remote
 * frame to ensure a client slot cannot take ownership of League's native Metal drawable. */
#import <Foundation/Foundation.h>
#import <CoreFoundation/CoreFoundation.h>
#import <objc/runtime.h>
#include <dlfcn.h>
#include <assert.h>
#include <stdio.h>

@interface FrameOwner : NSObject
@property(copy) NSString *title;
- (void)platformWindowDidInvalidateCGContext:(id)window;
@end
@implementation FrameOwner
@synthesize title;
- (void)platformWindowDidInvalidateCGContext:(id)window {}
@end
@interface NSObject (RemoteFrameProbe)
- (BOOL)presentRemoteCAFrame:(id)timer;
@end
static void *slot(id object, const char *name) {
    Ivar ivar = class_getInstanceVariable([object class], name);
    assert(ivar);
    return (char *)object + ivar_getOffset(ivar);
}
int main(int argc, char **argv) {
    @autoreleasepool {
        assert(argc == 2);
        void *backend = dlopen(argv[1], RTLD_LAZY);
        if (!backend) { fprintf(stderr,"%s\n",dlerror()); return 1; }
        Class cls = NSClassFromString(@"X11Window");
        assert(cls);
        id native = [cls alloc]; /* Deliberately skip X11 initialization: cached-frame path needs no display. */
        FrameOwner *owner = [FrameOwner new];
        *(id *)slot(native,"_delegate") = owner;
        *(BOOL *)slot(native,"_mapped") = YES;
        *(BOOL *)slot(native,"_remoteFrameValid") = YES;
        *(unsigned *)slot(native,"_remoteFrameTargetCount") = 1;
        *(double *)slot(native,"_remoteFrameTargetRefresh") = CFAbsoluteTimeGetCurrent() + 60;
        *(double *)slot(native,"_remoteFrameSelectionRefresh") = CFAbsoluteTimeGetCurrent() + 60;
        owner.title = @"Riot Client";
        assert([native presentRemoteCAFrame:nil]); /* Client's cached presentation remains usable. */
        owner.title = @"League of Legends (TM) Client";
        assert(![native presentRemoteCAFrame:nil]); /* Same cached frame cannot cover the game. */
        [native release];
        [owner release];
        puts("REMOTE_FRAME_OWNERSHIP client_preserved=1 native_game_protected=1");
    }
    return 0;
}
