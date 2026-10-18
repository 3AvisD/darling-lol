/* Standalone check for the in-process X11 drag loop (-[NSWindow _localDragImage:...]).
 * Opens its own windows, drives -dragImage:at:offset:event:pasteboard:source:slideBack:
 * with synthetic NSEvents posted to this app's own queue, and asserts the outcome.
 * It injects no X11 input and touches no other client. A hang is reported as FAIL. */
#import <AppKit/AppKit.h>
#include <dispatch/dispatch.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#define CASE_TIMEOUT_SECONDS 20.0

@interface DragLog : NSObject {
 @public
  int entered, updated, exited, prepared, performed, concluded, ended, mouseDowns;
  NSDragOperation endOperation;
  BOOL removeWhenUpdated;
}
@end
@implementation DragLog @end

@interface NSView (DragCheckDefaults)
- (BOOL)prepareForDragOperation:(id<NSDraggingInfo>)sender;
@end

/* Destination without -prepareForDragOperation:, so the NSView default applies. */
@interface DropView : NSView { @public DragLog *log; }
@end
@implementation DropView
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)s { log->entered++; return NSDragOperationCopy; }
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)s { log->updated++; return NSDragOperationCopy; }
- (void)draggingExited:(id<NSDraggingInfo>)s { log->exited++; }
- (BOOL)performDragOperation:(id<NSDraggingInfo>)s { log->performed++; return YES; }
- (void)concludeDragOperation:(id<NSDraggingInfo>)s { log->concluded++; }
- (void)mouseDown:(NSEvent *)e { log->mouseDowns++; }
@end

/* Source that, like Chromium's, does not implement -pasteboardChangedOwner:. */
@interface DragSource : NSObject { @public DragLog *log; }
@end
@implementation DragSource
- (NSDragOperation)draggingSourceOperationMask { return NSDragOperationEvery; }
- (NSDragOperation)draggingSourceOperationMaskForLocal:(BOOL)local { return NSDragOperationEvery; }
- (void)draggedImage:(NSImage *)image endedAt:(NSPoint)p operation:(NSDragOperation)op {
  log->ended++; log->endOperation = op;
}
@end

static const char *currentCase = "startup";
static volatile time_t caseStart;
static int failures = 0;

static void *watchdog(void *unused) {
  for (;;) {
    usleep(100000);
    if (time(NULL) - caseStart > CASE_TIMEOUT_SECONDS) {
      printf("FAIL %s: no progress for %.0fs (drag hang)\n", currentCase, CASE_TIMEOUT_SECONDS);
      fflush(stdout);
      _exit(1);
    }
  }
  return NULL;
}
static void beginCase(const char *name) { currentCase = name; caseStart = time(NULL); }
static void expect(BOOL ok, const char *what) {
  if (!ok) { failures++; printf("  FAIL %s: %s\n", currentCase, what); }
}
static void endCase(int failuresBefore) {
  printf("%s %s\n", failures == failuresBefore ? "PASS" : "FAIL", currentCase);
  fflush(stdout);
}

static void pump(double seconds) {
  NSDate *end = [NSDate dateWithTimeIntervalSinceNow:seconds];
  while ([end timeIntervalSinceNow] > 0) {
    NSEvent *e = [NSApp nextEventMatchingMask:NSAnyEventMask untilDate:[NSDate distantPast]
                                       inMode:NSDefaultRunLoopMode dequeue:YES];
    if (e) [NSApp sendEvent:e];
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.005]];
  }
}

static NSWindow *makeWindow(NSString *title, NSRect frame) {
  NSWindow *w = [[NSWindow alloc] initWithContentRect:frame styleMask:NSBorderlessWindowMask
                                              backing:NSBackingStoreBuffered defer:NO];
  [w setTitle:title];
  [w setReleasedWhenClosed:NO];
  [w makeKeyAndOrderFront:nil];
  return w;
}

/* Destination window holding a registered DropView that only its superview owns. */
static NSWindow *makeDestination(DragLog *log, DropView **viewOut) {
  NSWindow *w = makeWindow(@"Darling drag check destination", NSMakeRect(500, 200, 240, 240));
  DropView *v = [[DropView alloc] initWithFrame:NSMakeRect(20, 20, 200, 200)];
  v->log = log;
  [v registerForDraggedTypes:[NSArray arrayWithObject:NSStringPboardType]];
  [[w contentView] addSubview:v];
  if (viewOut) *viewOut = v;
  [v release];
  return w;
}

static NSEvent *mouse(NSEventType type, NSWindow *w, NSPoint p) {
  return [NSEvent mouseEventWithType:type location:p modifierFlags:0
                           timestamp:[[NSProcessInfo processInfo] systemUptime]
                        windowNumber:[w windowNumber] context:nil eventNumber:0
                          clickCount:1 pressure:1.0];
}
static NSEvent *escapeKey(NSWindow *w) {
  return [NSEvent keyEventWithType:NSKeyDown location:NSMakePoint(0, 0) modifierFlags:0
                         timestamp:[[NSProcessInfo processInfo] systemUptime]
                      windowNumber:[w windowNumber] context:nil characters:@"\033"
       charactersIgnoringModifiers:@"\033" isARepeat:NO keyCode:53];
}
static void post(NSEvent *e) { [NSApp postEvent:e atStart:NO]; }

static NSPasteboard *declarePasteboard(id owner) {
  NSPasteboard *pb = [NSPasteboard pasteboardWithName:NSDragPboard];
  [pb declareTypes:[NSArray arrayWithObject:NSStringPboardType] owner:owner];
  [pb setString:@"darling drag check" forType:NSStringPboardType];
  return pb;
}

static void runDrag(NSWindow *sourceWindow, DragSource *source) {
  NSPasteboard *pb = declarePasteboard(source);
  NSImage *image = [[[NSImage alloc] initWithSize:NSMakeSize(32, 32)] autorelease];
  NSEvent *down = mouse(NSLeftMouseDown, sourceWindow, NSMakePoint(50, 50));
  [sourceWindow dragImage:image at:NSMakePoint(50, 50) offset:NSZeroSize event:down
               pasteboard:pb source:source slideBack:YES];
}

static NSPoint inside = {120, 120}; /* inside DropView, in destination window base coordinates */
static NSPoint outside = {-4000, -4000};

int main(int argc, const char **argv) {
  @autoreleasepool {
    [NSApplication sharedApplication];
    pthread_t thread;
    beginCase("startup");
    pthread_create(&thread, NULL, watchdog, NULL);
    NSWindow *sourceWindow = makeWindow(@"Darling drag check source", NSMakeRect(100, 200, 240, 240));
    pump(0.3);
    int before;

    /* 1. Drop onto a destination that lacks prepareForDragOperation:. */
    {
      beginCase("1 drop onto destination"); before = failures;
      DragLog *log = [[DragLog alloc] init]; DragSource *src = [[DragSource alloc] init]; src->log = log;
      DropView *view; NSWindow *dw = makeDestination(log, &view); pump(0.2);
      expect([view prepareForDragOperation:nil] == YES, "NSView default prepareForDragOperation: is not YES");
      NSWindow *dest = dw;
      post(mouse(NSLeftMouseDragged, dest, inside));
      post(mouse(NSLeftMouseDragged, dest, NSMakePoint(inside.x + 2, inside.y)));
      post(mouse(NSLeftMouseUp, dest, inside));
      runDrag(sourceWindow, src);
      expect(log->entered == 1, "draggingEntered: not called exactly once");
      expect(log->performed == 1, "performDragOperation: not called exactly once");
      expect(log->concluded == 1, "concludeDragOperation: not called exactly once");
      expect(log->ended == 1, "draggedImage:endedAt:operation: not called exactly once");
      expect(log->endOperation != NSDragOperationNone, "end operation is None after a successful drop");
      [dw close]; [dw release]; [src release]; [log release];
      endCase(before);
    }

    /* 2. Drag leaves the destination and ends outside any window. */
    {
      beginCase("2 end outside destination"); before = failures;
      DragLog *log = [[DragLog alloc] init]; DragSource *src = [[DragSource alloc] init]; src->log = log;
      NSWindow *dw = makeDestination(log, NULL); pump(0.2);
      post(mouse(NSLeftMouseDragged, dw, inside));
      post(mouse(NSLeftMouseDragged, sourceWindow, outside));
      post(mouse(NSLeftMouseUp, sourceWindow, outside));
      runDrag(sourceWindow, src);
      expect(log->entered == 1, "draggingEntered: not called exactly once");
      expect(log->exited >= 1, "draggingExited: not called");
      expect(log->performed == 0, "performDragOperation: called for a drop outside");
      expect(log->ended == 1, "draggedImage:endedAt:operation: not called exactly once");
      expect(log->endOperation == NSDragOperationNone, "end operation is not None");
      [dw close]; [dw release]; [src release]; [log release];
      endCase(before);
    }

    /* 3. Escape cancels while over the destination. */
    {
      beginCase("3 escape cancel"); before = failures;
      DragLog *log = [[DragLog alloc] init]; DragSource *src = [[DragSource alloc] init]; src->log = log;
      NSWindow *dw = makeDestination(log, NULL); pump(0.2);
      post(mouse(NSLeftMouseDragged, dw, inside));
      post(escapeKey(sourceWindow));
      runDrag(sourceWindow, src);
      expect(log->performed == 0, "performDragOperation: called after Escape");
      expect(log->ended == 1, "draggedImage:endedAt:operation: not called exactly once");
      expect(log->endOperation == NSDragOperationNone, "end operation is not None");
      [dw close]; [dw release]; [src release]; [log release];
      endCase(before);
    }

    /* 4. Two drags back to back with sources lacking pasteboardChangedOwner:. */
    {
      beginCase("4 back-to-back drags"); before = failures;
      DragLog *log1 = [[DragLog alloc] init], *log2 = [[DragLog alloc] init];
      DragSource *s1 = [[DragSource alloc] init], *s2 = [[DragSource alloc] init];
      s1->log = log1; s2->log = log2;
      expect(![s1 respondsToSelector:@selector(pasteboardChangedOwner:)], "fixture source implements pasteboardChangedOwner:");
      NSWindow *dw = makeDestination(log1, NULL); pump(0.2);
      post(mouse(NSLeftMouseDragged, dw, inside)); post(mouse(NSLeftMouseUp, dw, inside));
      runDrag(sourceWindow, s1);
      post(mouse(NSLeftMouseDragged, dw, inside)); post(mouse(NSLeftMouseUp, dw, inside));
      runDrag(sourceWindow, s2); /* declaring new owner notifies s1, which must be tolerated */
      expect(log1->ended == 1, "first drag did not end exactly once");
      expect(log2->ended == 1, "second drag did not end exactly once");
      expect(log1->performed == 2, "destination did not receive both drops");
      [dw close]; [dw release]; [s1 release]; [s2 release]; [log1 release]; [log2 release];
      endCase(before);
    }

    /* 5. Main queue and run loop timers are serviced during the drag. */
    {
      beginCase("5 main queue and timer serviced"); before = failures;
      DragLog *log = [[DragLog alloc] init]; DragSource *src = [[DragSource alloc] init]; src->log = log;
      NSWindow *dw = makeDestination(log, NULL); pump(0.2);
      __block BOOL queueFired = NO, timerFired = NO, inDrag = YES;
      __block BOOL queueDuring = NO, timerDuring = NO;
      CFRunLoopTimerRef timer = CFRunLoopTimerCreateWithHandler(NULL, CFAbsoluteTimeGetCurrent() + 0.03, 0, 0, 0,
          ^(CFRunLoopTimerRef t) { timerFired = YES; timerDuring = inDrag; });
      CFRunLoopAddTimer(CFRunLoopGetMain(), timer, kCFRunLoopDefaultMode);
      dispatch_async(dispatch_get_main_queue(), ^{ queueFired = YES; queueDuring = inDrag; });
      post(mouse(NSLeftMouseDragged, dw, inside)); post(mouse(NSLeftMouseUp, dw, inside));
      runDrag(sourceWindow, src);
      inDrag = NO;
      expect(queueDuring, queueFired ? "dispatch_async block ran only after the drag returned" : "dispatch_async block never ran");
      expect(timerDuring, timerFired ? "run loop timer fired only after the drag returned" : "run loop timer never fired");
      CFRunLoopTimerInvalidate(timer); CFRelease(timer);
      expect(log->ended == 1, "drag did not end exactly once");
      [dw close]; [dw release]; [src release]; [log release];
      endCase(before);
    }

    /* 6. Mouse input reaches views normally after the drags. */
    {
      beginCase("6 input after drags"); before = failures;
      DragLog *log = [[DragLog alloc] init];
      DropView *view; NSWindow *dw = makeDestination(log, &view); pump(0.2);
      post(mouse(NSLeftMouseDown, dw, inside)); post(mouse(NSLeftMouseUp, dw, inside));
      pump(0.3);
      expect(log->mouseDowns == 1, "mouse-down was not delivered to the view after drags");
      [dw close]; [dw release]; [log release];
      endCase(before);
    }

    /* 7. Destination view removed (and window closed) from a main-queue block mid-drag. */
    {
      beginCase("7 destination removed mid-drag"); before = failures;
      DragLog *log = [[DragLog alloc] init]; DragSource *src = [[DragSource alloc] init]; src->log = log;
      DropView *view; NSWindow *dw = makeDestination(log, &view); pump(0.2);
      post(mouse(NSLeftMouseDragged, dw, inside)); post(mouse(NSLeftMouseUp, dw, inside));
      dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
        [view removeFromSuperview];   /* the drag loop must have retained it */
        [dw orderOut:nil];
        [dw close];
      });
      runDrag(sourceWindow, src);
      expect(log->ended == 1, "drag did not end exactly once after the destination went away");
      pump(0.2);
      [dw release]; [src release]; [log release];
      endCase(before);
    }

    [sourceWindow close]; [sourceWindow release];
    printf("%s x11 drag check (%d failure%s)\n", failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
  }
}
