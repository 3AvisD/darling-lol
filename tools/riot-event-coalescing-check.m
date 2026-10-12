#import <AppKit/NSDisplay.h>
#import <AppKit/NSEvent.h>
#import <Foundation/Foundation.h>
#include <assert.h>
#include <stdio.h>
@interface ProbeDisplay : NSDisplay
@end
@implementation ProbeDisplay
@end
int main(void) {
 @autoreleasepool {
  NSDisplay *display=[[ProbeDisplay alloc] init];
  NSEvent *key=[NSEvent keyEventWithType:NSKeyDown location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:0 context:nil characters:@"probe" charactersIgnoringModifiers:@"probe" isARepeat:NO keyCode:0];
  NSEvent *old=[NSEvent mouseEventWithType:NSMouseMoved location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:0 context:nil eventNumber:0 clickCount:0 pressure:0];
  NSEvent *latest=[NSEvent mouseEventWithType:NSMouseMoved location:NSMakePoint(1,1) modifierFlags:0 timestamp:1 windowNumber:0 context:nil eventNumber:1 clickCount:0 pressure:0];
  [display postEvent:key atStart:NO];[display postEvent:old atStart:NO];[display postEvent:latest atStart:NO];
  [display discardEventsMatchingMask:NSMouseMovedMask beforeEvent:latest];
  BOOL keyPreserved=[display nextEventMatchingMask:NSAnyEventMask untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES]==key;
  BOOL latestPreserved=[display nextEventMatchingMask:NSAnyEventMask untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES]==latest;
  /* An empty Darling queue returns its NSAppKitSystem wakeup sentinel. */
  BOOL oldRemoved=[[display nextEventMatchingMask:NSAnyEventMask untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES] type]==NSAppKitSystem;
  [display postEvent:key atStart:NO]; [display postEvent:latest atStart:NO];
  BOOL peekPreserved=[display nextEventMatchingMask:NSMouseMovedMask untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:NO]==latest;
  BOOL filteredMotion=[display nextEventMatchingMask:NSMouseMovedMask untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES]==latest;
  BOOL filteredKey=[display nextEventMatchingMask:NSKeyDownMask untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES]==key;
  printf("EVENT_FILTERING peek_preserved=%d matching_motion=%d unmatched_key_preserved=%d\n",peekPreserved,filteredMotion,filteredKey);
  printf("EVENT_COALESCING key_preserved=%d older_motion_removed=%d latest_motion_preserved=%d\n",keyPreserved,oldRemoved,latestPreserved);
  [display release];
  return keyPreserved&&latestPreserved&&oldRemoved&&peekPreserved&&filteredMotion&&filteredKey?0:1;
 }
 return 0;
}
