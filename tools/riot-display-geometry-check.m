/* Exercise the real X11/CG methods with synthetic cached monitor layouts.
 * No input is sent: pointer warps are captured by a temporary method override. */
#import <AppKit/AppKit.h>
#import <AppKit/NSDisplay.h>
#import <AppKit/NSScreen.h>
#import <CoreGraphics/CGDirectDisplay.h>
#import <objc/runtime.h>
#import "../build/darling-src/src/external/cocotron/AppKit/X11.backend/X11Display.h"
#import "../build/darling-src/src/external/cocotron/AppKit/X11.backend/X11Window.h"
#include <X11/extensions/Xrandr.h>
#include <X11/Xatom.h>
#include <assert.h>
#include <string.h>

// Implemented by Cocotron but absent from its public headers.
extern CGRect CGDisplayBounds(CGDirectDisplayID display);
extern CGError CGWarpMouseCursorPosition(CGPoint position);

@interface X11Display (GeometryProbe)
- (NSArray *)_rootScreenFallback;
- (void)postXEvent:(XEvent *)event;
@end

@interface X11Window (FullscreenProbe)
- (BOOL)wantsFullscreenWithStyleMask:(NSUInteger)mask rootFrame:(O2Rect)frame;
@end
@interface FullscreenProbeWindow : NSWindow
@end
@implementation FullscreenProbeWindow
- (BOOL)canBecomeKeyWindow { return YES; }
@end
static BOOL fullscreenProperty(X11Display *display, X11Window *window) {
    Display *d = [display display];
    Atom actual, fs = XInternAtom(d,"_NET_WM_STATE_FULLSCREEN",False);
    int format; unsigned long count, remaining; unsigned char *data = NULL;
    assert(XGetWindowProperty(d,[window windowHandle],XInternAtom(d,"_NET_WM_STATE",False),
        0,32,False,XA_ATOM,&actual,&format,&count,&remaining,&data) == Success);
    BOOL found = NO;
    assert(actual == XA_ATOM && format == 32 && remaining == 0);
    for (unsigned long i=0; i<count; ++i) if (((Atom *)data)[i] == fs) found=YES;
    if (data) XFree(data);
    return found;
}

static void *slot(id obj, const char *name) {
    Ivar ivar = class_getInstanceVariable([obj class], name);
    assert(ivar);
    return (char *)obj + ivar_getOffset(ivar);
}
static void rectEqual(NSRect actual, NSRect expected) {
    assert(NSEqualRects(actual, expected));
}
static unsigned captureWarp(id obj, SEL sel, int x, int y) {
    assert(x == 3940 && y == 1010);
    return 1;
}
static NSScreen *screen(NSRect frame, unsigned displayID) {
    NSScreen *s = [[[NSScreen alloc] initWithFrame:frame visibleFrame:frame] autorelease];
    [s setCgDirectDisplayID:displayID];
    return s;
}
int main(void) {
 setbuf(stdout,NULL);
 @autoreleasepool {
    [NSApplication sharedApplication];
    X11Display *d = (id)[NSDisplay currentDisplay];
    assert([d respondsToSelector:@selector(rootRectForFrame:)]);
    NSArray *actualScreens = [d screens];
    assert([actualScreens count] > 0);
    NSScreen *first = [actualScreens objectAtIndex:0];
    if (!NSIsEmptyRect([first frame])) {
        assert(NSMinX([first frame]) == 0 && NSMinY([first frame]) == 0);
        assert(CGMainDisplayID() == [first cgDirectDisplayID]);
    }
    puts("DISPLAY_GEOMETRY live_screen_origin=pass");

    NSRect layouts[] = { NSMakeRect(0,0,1920,1080), NSMakeRect(3840,810,3840,2160),
                         NSMakeRect(0,1200,1920,1080), NSMakeRect(1920,0,1920,1080) };
    for (unsigned i = 0; i < sizeof layouts / sizeof *layouts; i++) {
        *(NSRect *)slot(d,"_primaryRootFrame") = layouts[i];
        *(BOOL *)slot(d,"_primaryRootFrameValid") = YES;
        NSRect frame = NSMakeRect(-320,140,800,600);
        NSRect root = [d rootRectForFrame:frame];
        rectEqual(root, NSMakeRect(NSMinX(layouts[i])-320,NSMaxY(layouts[i])-740,800,600));
        rectEqual([d frameForRootRect:root],frame);
        // Test X11Window's inverse as well, using an unmapped object.
        X11Window *w = [NSClassFromString(@"X11Window") alloc];
        *(NSRect *)slot(w,"_frame") = root;
        rectEqual([w frame],frame);
        [w release];
    }
    puts("DISPLAY_GEOMETRY single_offset_vertical_negative_roundtrip=pass");
    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(100,100,800,600)
        styleMask:NSBorderlessWindowMask backing:NSBackingStoreBuffered defer:NO];
    X11Window *native = (id)[window platformWindow];
    rectEqual(*(NSRect *)slot(native,"_frame"),NSMakeRect(2020,380,800,600));
    [native setFrame:NSMakeRect(-100,200,640,480)];
    rectEqual(*(NSRect *)slot(native,"_frame"),NSMakeRect(1820,400,640,480));
    rectEqual([native frame],NSMakeRect(-100,200,640,480));
    [window release];
    puts("DISPLAY_GEOMETRY unmapped_window_create_resize=pass");

    NSRect primary = NSMakeRect(3840,810,3840,2160);
    *(NSRect *)slot(d,"_primaryRootFrame") = primary;
    NSArray **cache = slot(d,"_lastScreens");
    [*cache release];
    *cache = [@[
        screen(NSMakeRect(0,0,3840,2160),2),
        screen(NSMakeRect(-3840,102,3840,2160),1),
        screen(NSMakeRect(3840,-230,1800,3200),3),
        screen(NSZeroRect,4)
    ] retain];
    assert(CGMainDisplayID() == 2 && CGDisplayIsMain(2) && !CGDisplayIsMain(1));
    assert(CGDisplayPixelsWide(3) == 1800 && CGDisplayPixelsHigh(3) == 3200);
    assert(CGDisplayPixelsWide(99) == 0);
    rectEqual(CGDisplayBounds(1),NSMakeRect(-3840,-102,3840,2160));
    rectEqual(CGDisplayBounds(3),NSMakeRect(3840,-810,1800,3200));
    rectEqual(CGDisplayBounds(4),NSZeroRect);
    CGDirectDisplayID ids[4] = {0}; uint32_t count;
    assert(CGGetActiveDisplayList(4,ids,&count) == 0 && count == 3 && ids[0] == 2 && ids[1] == 1);
    assert(CGGetOnlineDisplayList(1,ids,&count) == 0 && count == 4 && ids[0] == 2);
    assert(CGGetActiveDisplayList(0,NULL,&count) == 0 && count == 3);
    assert(CGGetDisplaysWithPoint(CGPointMake(-100,-50),4,ids,&count) == 0 && count == 1 && ids[0] == 1);
    assert(CGGetDisplaysWithRect(CGRectMake(3900,-800,10,10),4,ids,&count) == 0 && count == 1 && ids[0] == 3);
    puts("DISPLAY_GEOMETRY reordered_ids_bounds_enumeration_queries=pass");

    // Use cached monitor frames so the policy is tested for the reporter's layout.
    FullscreenProbeWindow *game = [[FullscreenProbeWindow alloc]
        initWithContentRect:NSMakeRect(0,0,3840,2160) styleMask:NSBorderlessWindowMask
        backing:NSBackingStoreBuffered defer:NO];
    X11Window *gameNative = (id)[game platformWindow];
    assert(!*(BOOL *)slot(gameNative,"_overrideRedirect"));
    assert(fullscreenProperty(d,gameNative));
    [gameNative setFrame:NSMakeRect(0,0,3840,2100)]; // panel-sized work area is not a monitor
    assert(!fullscreenProperty(d,gameNative));
    [gameNative setFrame:NSMakeRect(-3840,102,3840,2160)]; // secondary monitor
    assert(fullscreenProperty(d,gameNative));
    NSRect full = [d rootRectForFrame:NSMakeRect(0,0,3840,2160)];
    assert(![gameNative wantsFullscreenWithStyleMask:NSTitledWindowMask rootFrame:full]);
    assert([gameNative wantsFullscreenWithStyleMask:NSWindowStyleMaskFullScreen
        rootFrame:NSMakeRect(1,2,640,480)]);
    *(BOOL *)slot(gameNative,"_overrideRedirect") = YES;
    assert(![gameNative wantsFullscreenWithStyleMask:0 rootFrame:full]);
    *(BOOL *)slot(gameNative,"_overrideRedirect") = NO;
    [gameNative setFrame:NSMakeRect(20,30,800,600)];
    assert(!fullscreenProperty(d,gameNative));
    [game release];
    puts("DISPLAY_GEOMETRY borderless_fullscreen_monitor_and_resize_policy=pass");

    Method method = class_getInstanceMethod([d class],@selector(warpPointerToRootX:y:));
    assert(method);
    IMP original = method_setImplementation(method,(IMP)captureWarp);
    X11RelativeMouse *relative = slot(d,"_relativeMouse");
    X11RelativeMouseBegin(relative,3900,1000);
    assert(CGWarpMouseCursorPosition(CGPointMake(100,200)) == 0);
    BOOL honoured = !*(BOOL *)slot(d,"_isXwayland") || *(BOOL *)slot(d,"_cursorHidden");
    assert(relative->virtualX == (honoured ? 3940 : 3900) && relative->virtualY == (honoured ? 1010 : 1000));
    NSPoint location = [d mouseLocation];
    assert(location.x == (honoured ? 100 : 60) && location.y == (honoured ? 1960 : 1970));
    X11RelativeMouseMotion motion = X11RelativeMouseHandleMotion(relative,3907,1009,false,NextRequest([d display]));
    assert(motion.deltaX == 7 && motion.deltaY == 9);
    X11RelativeMouseEnd(relative);
    assert(CGWarpMouseCursorPosition(CGPointMake(100,200)) == 0);
    method_setImplementation(method,original);
    puts("DISPLAY_GEOMETRY absolute_relative_warp_and_delta=pass");
    int eventBase = *(int *)slot(d,"_rrEventBase");
    assert(eventBase);
    XEvent notify = {0}; notify.type = eventBase + RRNotify;
    notify.xany.display = [d display];
    [d postXEvent:&notify];
    assert(!*(BOOL *)slot(d,"_primaryRootFrameValid") && !*cache);
    assert([[d screens] count] > 0 && *(BOOL *)slot(d,"_primaryRootFrameValid"));
    XRRScreenChangeNotifyEvent change = {0};
    Display *xdisplay = [d display];
    change.type = eventBase + RRScreenChangeNotify;
    change.display = xdisplay; change.root = DefaultRootWindow(xdisplay);
    change.width = DisplayWidth(xdisplay,DefaultScreen(xdisplay));
    change.height = DisplayHeight(xdisplay,DefaultScreen(xdisplay));
    change.mwidth = DisplayWidthMM(xdisplay,DefaultScreen(xdisplay));
    change.mheight = DisplayHeightMM(xdisplay,DefaultScreen(xdisplay));
    change.rotation = RR_Rotate_0;
    [d postXEvent:(XEvent *)&change];
    assert(!*(BOOL *)slot(d,"_primaryRootFrameValid") && !*cache);
    NSArray *fallback = [d _rootScreenFallback];
    assert([fallback count] == 1 && [[fallback objectAtIndex:0] cgDirectDisplayID] == 1);
    rectEqual(*(NSRect *)slot(d,"_primaryRootFrame"),NSMakeRect(0,0,change.width,change.height));
    puts("DISPLAY_GEOMETRY cache_events_and_fallback=pass");
 }
 return 0;
}
