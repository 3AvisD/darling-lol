#import <AppKit/AppKit.h>
#include <stdio.h>

int main(void) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        NSRect rect = NSMakeRect(20, 20, 1280, 720);
        NSUInteger mask = NSTitledWindowMask | NSWindowStyleMaskFullSizeContentView;
        NSRect converted = [NSWindow contentRectForFrameRect:
            [NSWindow frameRectForContentRect:rect styleMask:mask] styleMask:mask];
        NSWindow *full = [[NSWindow alloc] initWithContentRect:rect
            styleMask:mask backing:NSBackingStoreBuffered defer:NO];
        [full setFrame:rect display:NO];
        NSSize size = [[full contentView] frame].size;
        BOOL pass = NSEqualRects(converted, rect) && NSEqualSizes(size, rect.size);
        NSWindow *ordinary = [[NSWindow alloc] initWithContentRect:rect
            styleMask:NSTitledWindowMask backing:NSBackingStoreBuffered defer:NO];
        pass &= NSEqualSizes([[ordinary contentView] frame].size, rect.size);
        [ordinary setStyleMask:mask];
        NSSize changed = [[ordinary contentView] frame].size;
        pass &= NSEqualSizes(changed, [ordinary frame].size);
        [full setFrame:NSMakeRect(20, 20, 900, 500) display:NO];
        pass &= NSEqualSizes([[full contentView] frame].size, NSMakeSize(900, 500));
        printf("WINDOW_LAYOUT_CHECK full=%.0fx%.0f changed=%.0fx%.0f pass=%d\n",
            size.width, size.height, changed.width, changed.height, pass);
        [ordinary release]; [full release];
        return pass ? 0 : 1;
    }
}
