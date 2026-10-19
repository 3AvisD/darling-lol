/* Local event dispatch regression; also runs unchanged against native AppKit.
 * No global input, client hooks, account access, or mapped windows. */
#import <AppKit/AppKit.h>

static NSMutableString *calls;
static int failures;

@interface MonitorTestWindow : NSWindow
@end
@implementation MonitorTestWindow
- (void) sendEvent: (NSEvent *) event {
    [calls appendFormat: @"W%ld", (long)[event type]];
}
@end

static void expectCalls(NSString *name, NSString *expected) {
    BOOL ok = [calls isEqual: expected];
    printf("%s %s got=%s expected=%s\n", ok ? "PASS" : "FAIL",
           [name UTF8String], [calls UTF8String], [expected UTF8String]);
    failures += !ok;
    [calls setString: @""];
}

/* AppKit does not promise an order between independent registrations. Native
 * ordering can change with block addresses, even on the same OS build. */
static void expectBoth(NSString *name) {
    NSString *expected = [calls isEqual: @"ABW1"] ? @"ABW1" : @"BAW1";
    expectCalls(name, expected);
}

int main(void) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        calls = [NSMutableString new];
        NSWindow *window = [[MonitorTestWindow alloc]
                initWithContentRect: NSMakeRect(100, 100, 300, 200)
                styleMask: 0 backing: NSBackingStoreBuffered defer: NO];
        NSEvent *down = [NSEvent mouseEventWithType: NSLeftMouseDown
                location: NSMakePoint(10, 10) modifierFlags: 0 timestamp: 0
                windowNumber: [window windowNumber] context: nil
                eventNumber: 1 clickCount: 1 pressure: 1];
        NSEvent *up = [NSEvent mouseEventWithType: NSLeftMouseUp
                location: NSMakePoint(10, 10) modifierFlags: 0 timestamp: 0
                windowNumber: [window windowNumber] context: nil
                eventNumber: 2 clickCount: 1 pressure: 0];
        id a = [NSEvent addLocalMonitorForEventsMatchingMask: NSLeftMouseDownMask
                handler: ^NSEvent *(NSEvent *event) {
                    [calls appendString: @"A"]; return event;
                }];
        id b = [NSEvent addLocalMonitorForEventsMatchingMask: NSLeftMouseDownMask
                handler: ^NSEvent *(NSEvent *event) {
                    [calls appendString: @"B"]; return event;
                }];
        [NSApp sendEvent: down]; expectBoth(@"multiple-monitors");
        [NSApp sendEvent: up]; expectCalls(@"mask-filter", @"W2");
        [NSEvent removeMonitor: b];
        [NSApp sendEvent: down]; expectCalls(@"remove", @"AW1");
        [NSEvent removeMonitor: a];

        b = [NSEvent addLocalMonitorForEventsMatchingMask: NSLeftMouseDownMask
                handler: ^NSEvent *(NSEvent *event) {
                    [calls appendString: @"B"]; return nil;
                }];
        [NSApp sendEvent: down]; expectCalls(@"consume", @"B");
        [NSEvent removeMonitor: b];
        b = [NSEvent addLocalMonitorForEventsMatchingMask: NSLeftMouseDownMask
                handler: ^NSEvent *(NSEvent *event) {
                    [calls appendString: @"B"]; return up;
                }];
        [NSApp sendEvent: down]; expectCalls(@"replace-event", @"BW2");
        [NSEvent removeMonitor: b];

        __block id selfRemoving;
        selfRemoving = [NSEvent addLocalMonitorForEventsMatchingMask: NSLeftMouseDownMask
                handler: ^NSEvent *(NSEvent *event) {
                    [calls appendString: @"S"];
                    [NSEvent removeMonitor: selfRemoving]; return event;
                }];
        [NSApp sendEvent: down]; expectCalls(@"self-remove", @"SW1");
        [NSApp sendEvent: down]; expectCalls(@"self-remove-next", @"W1");
        __block id pendingA, pendingB;
        pendingA = [NSEvent addLocalMonitorForEventsMatchingMask: NSLeftMouseDownMask
                handler: ^NSEvent *(NSEvent *event) {
                    [calls appendString: @"A"];
                    [NSEvent removeMonitor: pendingB]; return event;
                }];
        pendingB = [NSEvent addLocalMonitorForEventsMatchingMask: NSLeftMouseDownMask
                handler: ^NSEvent *(NSEvent *event) {
                    [calls appendString: @"B"];
                    [NSEvent removeMonitor: pendingA]; return event;
                }];
        [NSApp sendEvent: down]; expectBoth(@"remove-pending-snapshot");
        [NSApp sendEvent: down]; expectCalls(@"remove-pending-next", @"W1");
        [window release];
        [calls release];
    }
    return failures ? 1 : 0;
}
