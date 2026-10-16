#include <CoreGraphics/CoreGraphics.h>
#include <stdio.h>

/* Exported by Darling's CoreGraphics but missing from its CGDirectDisplay.h. */
CFArrayRef CGDisplayCopyAllDisplayModes(CGDirectDisplayID display, CFDictionaryRef options);

/* League passes CGDisplayCopyAllDisplayModes' result straight to
 * CFArrayGetCount; display id 0 comes from an off-screen window. */
static int check(CGDirectDisplayID display) {
    CFArrayRef modes = CGDisplayCopyAllDisplayModes(display, NULL);
    if (!modes) {
        printf("DISPLAY_MODES display=%u result=NULL pass=0\n", display);
        return 0;
    }
    printf("DISPLAY_MODES display=%u count=%ld\n", display, (long)CFArrayGetCount(modes));
    CFRelease(modes);
    return 1;
}

/* League divides by the physical size for its camera aspect ratio; {0,0}
 * made every 3D vertex NaN. */
static int checkSize(CGDirectDisplayID display) {
    CGSize size = CGDisplayScreenSize(display);
    int ok = size.width > 0 && size.height > 0;
    printf("SCREEN_SIZE display=%u size=%.0fx%.0fmm pass=%d\n", display, size.width, size.height, ok);
    return ok;
}

int main(void) {
    int pass = check(CGMainDisplayID()) & check(0) & check(0xffff);
    pass &= checkSize(CGMainDisplayID()) & checkSize(0) & checkSize(0xffff);
    printf("DISPLAY_MODES_CHECK pass=%d\n", pass);
    return !pass;
}
