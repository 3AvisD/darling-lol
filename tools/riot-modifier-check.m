#import <AppKit/AppKit.h>
#import <AppKit/NSDisplay.h>
#import <objc/runtime.h>
#include <X11/Xlib.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
@interface NSDisplay (ModifierProbe)
- (void)postXEvent:(XEvent *)event;
@end
static NSMutableArray *events;
static void capture(id display, SEL selector, NSEvent *event, BOOL first) {
    [events addObject:event];
}
static void key(NSDisplay *display, int type, unsigned int code, unsigned int state) {
    XEvent event={0}; event.type=type; event.xkey.keycode=code;event.xkey.state=state;
    [display postXEvent:&event];
}
static void expectModifier(unsigned short code, NSUInteger flags) {
    NSEvent *event=[events lastObject];
    assert([event type]==NSFlagsChanged);
    assert([event keyCode]==code);
    assert([event modifierFlags]==flags);
}
int main(void) {
 @autoreleasepool {
    [NSApplication sharedApplication];
    NSDisplay *display=[NSDisplay currentDisplay];
    printf("APPKIT_BUNDLE %s\n",[[[NSBundle bundleForClass:[NSDisplay class]] bundlePath] UTF8String]);
    printf("BACKEND modifier_reconciliation=%d\n",[display respondsToSelector:@selector(reconcileModifierKeys:window:)]);fflush(stdout);
    printf("DISPLAY_CLASS %s BUNDLE %s\n",class_getName([display class]),[[[NSBundle bundleForClass:[display class]] bundlePath] UTF8String]);fflush(stdout);sleep(5);
    events=[NSMutableArray new];
    Method method=class_getInstanceMethod([display class],@selector(postEvent:atStart:));
    IMP original=method_setImplementation(method,(IMP)capture);
    key(display,KeyPress,133,0);expectModifier(0x37,NSCommandKeyMask);
    key(display,KeyPress,134,Mod4Mask);expectModifier(0x36,NSCommandKeyMask);
    key(display,KeyRelease,133,Mod4Mask);expectModifier(0x37,NSCommandKeyMask);
    key(display,KeyRelease,134,Mod4Mask);expectModifier(0x36,0);
    key(display,KeyPress,64,0);expectModifier(0x3a,NSAlternateKeyMask);
    key(display,KeyRelease,64,Mod1Mask);expectModifier(0x3a,0);
    key(display,KeyPress,50,0);expectModifier(0x38,NSShiftKeyMask);
    key(display,KeyPress,133,ShiftMask);expectModifier(0x37,NSShiftKeyMask|NSCommandKeyMask);
    XEvent focus={0}; focus.type=FocusOut; [display postXEvent:&focus];
    expectModifier(0x37,0);
    NSUInteger cleared=[events count]; [display postXEvent:&focus];
    assert([events count]==cleared);
    key(display,KeyPress,133,0);expectModifier(0x37,NSCommandKeyMask);
    XEvent map={0};map.type=KeymapNotify;[display postXEvent:&map];expectModifier(0x37,0);
    key(display,KeyPress,66,0);expectModifier(0x39,NSAlphaShiftKeyMask);
    key(display,KeyRelease,66,LockMask);expectModifier(0x39,NSAlphaShiftKeyMask);
    // GNOME overview: Super press delivered, release swallowed, no focus change.
    // The next event's server state lacks Mod4, so Command must be released first.
    key(display,KeyPress,133,LockMask);expectModifier(0x37,NSAlphaShiftKeyMask|NSCommandKeyMask);
    NSUInteger before=[events count];
    key(display,KeyPress,50,LockMask);
    assert([events count]==before+2);
    NSEvent *released=[events objectAtIndex:before];
    assert([released keyCode]==0x37 && [released modifierFlags]==NSAlphaShiftKeyMask);
    expectModifier(0x38,NSAlphaShiftKeyMask|NSShiftKeyMask);
    // A modifier that is still held stays held.
    key(display,KeyPress,133,LockMask|ShiftMask);expectModifier(0x37,NSAlphaShiftKeyMask|NSShiftKeyMask|NSCommandKeyMask);
    before=[events count];
    key(display,KeyRelease,50,LockMask|ShiftMask|Mod4Mask);
    assert([events count]==before+1);
    expectModifier(0x38,NSAlphaShiftKeyMask|NSCommandKeyMask);
    // Right Alt is Mod1 (Option) on US layouts and Mod5 (AltGr) elsewhere; the
    // backend reads the server map, so take whichever it reported and check it
    // is still held, and reported once, when Command goes up.
    key(display,KeyPress,108,LockMask|Mod4Mask);
    NSUInteger rightAlt=[[events lastObject] modifierFlags]&~(NSUInteger)(NSAlphaShiftKeyMask|NSCommandKeyMask);
    assert(rightAlt==NSAlternateKeyMask || rightAlt==NSFunctionKeyMask);
    unsigned int rightAltMask=rightAlt==NSAlternateKeyMask ? Mod1Mask : Mod5Mask;
    before=[events count];
    key(display,KeyRelease,133,LockMask|Mod4Mask|rightAltMask);
    assert([events count]==before+1);
    expectModifier(0x37,NSAlphaShiftKeyMask|rightAlt);
    method_setImplementation(method,original);
    puts("PASS Super=Command, Alt=Option, both Command keys, post-transition flags, focus-loss release, missed-release reconciliation, Caps Lock, swallowed Super release, layout-mapped Right Alt");
    [events release];
 }
 return 0;
}
