/* Protocol regression on a PRIVATE Xvfb server, never the user's DISPLAY. */
#ifndef X11_RELATIVE_MOUSE_HEADER
#define X11_RELATIVE_MOUSE_HEADER "../build/darling-src/src/external/cocotron/AppKit/X11.backend/X11RelativeMouse.h"
#endif
#include X11_RELATIVE_MOUSE_HEADER
#include <X11/Xlib.h>
#include <assert.h>
#include <stdio.h>

static XEvent nextMotion(Display *d) {
    XEvent event;
    do { XNextEvent(d, &event); } while (event.type != MotionNotify);
    return event;
}
static void movePhysical(Display *other, int x, int y) {
    XWarpPointer(other, None, DefaultRootWindow(other), 0, 0, 0, 0, x, y);
    XSync(other, False);
}
static X11RelativeMouseMotion handle(X11RelativeMouse *mouse, XEvent event) {
    return X11RelativeMouseHandleMotion(mouse, event.xmotion.x_root, event.xmotion.y_root, true,
                                        event.xmotion.serial);
}
static unsigned warp(Display *d, int x, int y) {
    XWarpPointer(d, None, DefaultRootWindow(d), 0, 0, 0, 0, x, y);
    XSync(d, False);
    return XEventsQueued(d, QueuedAlready);
}
int main(int argc, char **argv) {
    assert(argc == 2); /* The runner supplies the dedicated server explicitly. */
    Display *d = XOpenDisplay(argv[1]), *other = XOpenDisplay(argv[1]);
    assert(d && other);
    Window win = XCreateSimpleWindow(d, DefaultRootWindow(d), 1920, 24, 1920, 1056, 0, 0, 0);
    XSelectInput(d, win, PointerMotionMask);
    XMapWindow(d, win);
    XSync(d, False);
    movePhysical(other, 2500, 400);
    XSync(d, True);
    assert(XGrabPointer(d, win, False, PointerMotionMask, GrabModeAsync, GrabModeAsync,
                        win, None, CurrentTime) == GrabSuccess);
    /* Leave an older motion queued while taking the fresh grab snapshot. */
    movePhysical(other, 2510, 400);
    Window root, child; int rx, ry, wx, wy; unsigned state;
    unsigned long snapshot = NextRequest(d);
    assert(XQueryPointer(d, win, &root, &child, &rx, &ry, &wx, &wy, &state));
    X11RelativeMouse m = {0};
    X11RelativeMouseBegin(&m, rx, ry); m.firstSerial = snapshot;
    assert(rx == 2510 && ry == 400);
    X11RelativeMouseMotion r = handle(&m, nextMotion(d));
    assert(!r.post && !r.rewarp && m.physicalX == 2510);

    movePhysical(other, 2520, 395);
    r = handle(&m, nextMotion(d));
    assert(r.post && r.deltaX == 10 && r.deltaY == -5 && r.rewarp);
    unsigned long serial = NextRequest(d);
    unsigned queued = warp(d, r.warpX, r.warpY);
    X11RelativeMouseExpectEcho(&m, r.warpX, r.warpY, queued, serial);
    XEvent echo = nextMotion(d);
    assert(echo.xmotion.serial == serial);
    r = handle(&m, echo);
    assert(r.echo && !r.post && m.physicalX == 2510);
    puts("PASS: real Xorg grab snapshot and warp echo request serial");

    /* Queue real motion across the rewarp target BEFORE requesting a rewarp. */
    movePhysical(other, 2530, 400);
    movePhysical(other, 2510, 400);
    serial = NextRequest(d);
    queued = warp(d, 2510, 400);
    X11RelativeMouseExpectEcho(&m, 2510, 400, queued, serial);
    r = handle(&m, nextMotion(d));
    assert(r.post && !r.echo && r.deltaX == 20 && !r.rewarp);
    r = handle(&m, nextMotion(d));
    assert(r.post && !r.echo && r.deltaX == -20);
    r = handle(&m, nextMotion(d));
    assert(r.echo && !r.post);
    puts("PASS: real queued motion crossing the anchor is not swallowed as an echo");

    m.bounded = true; m.minX = 1920; m.minY = 24; m.maxX = 3839; m.maxY = 1079;
    double x = 3900, y = -100;
    X11RelativeMouseClamp(&m, &x, &y);
    /* Send the raw target here to verify the X server clips it exactly as the backend does. */
    serial = NextRequest(d); queued = warp(d, 3900, -100);
    X11RelativeMouseWarp(&m, x, y, queued, serial, true);
    assert(XQueryPointer(d, win, &root, &child, &rx, &ry, &wx, &wy, &state));
    assert(rx == 3839 && ry == 24 && m.virtualX == rx && m.virtualY == ry);
    r = handle(&m, nextMotion(d));
    assert(r.echo && !r.post);
    movePhysical(other, 3830, 30);
    r = handle(&m, nextMotion(d));
    assert(r.post && r.deltaX == -9 && r.deltaY == 6);
    puts("PASS: real Xorg confinement on a secondary monitor agrees with the virtual cursor");
    X11RelativeMouseEnd(&m);
    XUngrabPointer(d, CurrentTime);
    XDestroyWindow(d, win);
    XCloseDisplay(other); XCloseDisplay(d);
    return 0;
}
