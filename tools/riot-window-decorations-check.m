/* Standalone Cocoa/X11 regression: no client windows, hooks or input. */
#import <AppKit/AppKit.h>
#import <AppKit/NSDisplay.h>
#include <X11/Xlib.h>
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>

@protocol DecorationProbeDisplay
- (void *)display;
@end

@interface NSObject (DecorationProbe)
- (unsigned long)windowHandle;
- (void)recreateXWindowWithAlpha:(BOOL)alpha;
@end

static BOOL undecorated(NSWindow *window) {
    void *xlib = dlopen("/usr/lib/native/libX11.dylib", RTLD_LAZY);
    assert(xlib);
    Atom (*intern)(Display *, const char *, Bool) = dlsym(xlib, "XInternAtom");
    int (*sync)(Display *, Bool) = dlsym(xlib, "XSync");
    int (*get)(Display *, Window, Atom, long, long, Bool, Atom,
               Atom *, int *, unsigned long *, unsigned long *, unsigned char **) =
        dlsym(xlib, "XGetWindowProperty");
    int (*freeData)(void *) = dlsym(xlib, "XFree");
    assert(intern && sync && get && freeData);
    id native = [window platformWindow];
    Display *display = (Display *)[(id<DecorationProbeDisplay>)[NSDisplay currentDisplay] display];
    sync(display, False);
    Atom type = None;
    int format = 0;
    unsigned long count = 0, remaining = 0;
    unsigned char *data = NULL;
    int status = get(display, [native windowHandle], intern(display, "_MOTIF_WM_HINTS", False),
                     0, 5, False, AnyPropertyType, &type, &format, &count, &remaining, &data);
    assert(status == Success);
    BOOL result = format == 32 && count >= 3 && data &&
                  (((unsigned long *)data)[0] & 2) && ((unsigned long *)data)[2] == 0;
    if (data) freeData(data);
    dlclose(xlib);
    return result;
}

static void assertDecoration(NSWindow *window, BOOL expected, const char *name) {
    BOOL actual = undecorated(window);
    printf("WINDOW_DECORATION case=%s undecorated=%d expected=%d\n", name, actual, expected);
    assert(actual == expected);
}

int main(void) {
    setbuf(stdout, NULL);
    @autoreleasepool {
        [NSApplication sharedApplication];
        NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(20, 20, 320, 180)
            styleMask:NSTitledWindowMask backing:NSBackingStoreBuffered defer:NO];
        assertDecoration(window, NO, "ordinary titled window");
        [window setTitleVisibility:1];
        assertDecoration(window, NO, "hidden title alone preserves frame");
        [window setTitlebarAppearsTransparent:YES];
        assertDecoration(window, YES, "hidden and transparent title removes frame");
        [window setTitleVisibility:0];
        assertDecoration(window, NO, "visible title restores frame");
        [window setTitleVisibility:1];
        [window setTitlebarAppearsTransparent:NO];
        assertDecoration(window, NO, "opaque title restores frame");
        [window setStyleMask:NSTexturedBackgroundWindowMask];
        assertDecoration(window, YES, "untitled with extra flags");
        [window setStyleMask:NSTitledWindowMask];
        assertDecoration(window, NO, "titled style restored");
        [window setStyleMask:NSBorderlessWindowMask];
        assertDecoration(window, YES, "borderless after creation");
        [window setStyleMask:NSTitledWindowMask];
        [window setTitlebarAppearsTransparent:YES];
        [(id)[window platformWindow] recreateXWindowWithAlpha:NO];
        assertDecoration(window, YES, "recreated native window retains client chrome");
        NSWindow *deferred = [[NSWindow alloc] initWithContentRect:NSMakeRect(20, 20, 320, 180)
            styleMask:NSTitledWindowMask backing:NSBackingStoreBuffered defer:YES];
        [deferred setTitleVisibility:1];
        [deferred setTitlebarAppearsTransparent:YES];
        assertDecoration(deferred, YES, "properties set before native creation");
        puts("WINDOW_DECORATIONS_CHECK cases=10 pass=1");
        // Windows stay unmapped: the probe never changes compositor focus.
    }
    return 0;
}
