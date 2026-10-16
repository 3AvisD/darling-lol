/* Read-only XRandR geometry probe: never creates, maps or changes a window. */
#import <Foundation/Foundation.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <dlfcn.h>
#include <assert.h>
#include <stdio.h>
int main(int argc, char **argv) {
    @autoreleasepool {
        assert(argc == 2);
        void *backend = dlopen(argv[1], RTLD_LAZY);
        assert(backend);
        BOOL (*covers)(Display *, NSRect) = (BOOL (*)(Display *, NSRect))dlsym(backend,"X11RootRectCoversMonitor");
        assert(covers);
        assert(!covers(NULL, NSMakeRect(0,0,1,1)));
        assert(!covers(NULL, NSMakeRect(0,0,0,0)));
        void *xlib = dlopen("/usr/lib/native/libX11.dylib", RTLD_LAZY);
        void *randr = dlopen("/usr/lib/native/libXRandR.dylib", RTLD_LAZY);
        assert(xlib && randr);
        Display *(*openDisplay)(const char *) = dlsym(xlib,"XOpenDisplay");
        int (*closeDisplay)(Display *) = dlsym(xlib,"XCloseDisplay");
        Window (*rootWindow)(Display *) = dlsym(xlib,"XDefaultRootWindow");
        XRRScreenResources *(*getResources)(Display *,Window) = dlsym(randr,"XRRGetScreenResourcesCurrent");
        XRRCrtcInfo *(*getCrtc)(Display *,XRRScreenResources *,RRCrtc) = dlsym(randr,"XRRGetCrtcInfo");
        void (*freeCrtc)(XRRCrtcInfo *) = dlsym(randr,"XRRFreeCrtcInfo");
        void (*freeResources)(XRRScreenResources *) = dlsym(randr,"XRRFreeScreenResources");
        assert(openDisplay && closeDisplay && rootWindow && getResources && getCrtc && freeCrtc && freeResources);
        Display *d = openDisplay(NULL); assert(d);
        XRRScreenResources *resources = getResources(d,rootWindow(d)); assert(resources);
        unsigned checked = 0;
        for (int i=0; i<resources->ncrtc; ++i) {
            XRRCrtcInfo *c = getCrtc(d,resources,resources->crtcs[i]); assert(c);
            if (c->mode) {
                assert(covers(d,NSMakeRect(c->x,c->y,c->width,c->height)));
                assert(!covers(d,NSMakeRect(c->x,c->y,20,20)));
                assert(!covers(d,NSMakeRect(c->x,c->y,c->width/2,c->height/2)));
                ++checked;
            }
            freeCrtc(c);
        }
        freeResources(resources); closeDisplay(d);
        assert(checked);
        printf("FULLSCREEN_GEOMETRY startup_rejected=1 windowed_rejected=1 active_monitors=%u\n",checked);
    }
    return 0;
}
