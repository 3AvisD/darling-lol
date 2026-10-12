#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#include <dispatch/dispatch.h>
#include <mach/mach_time.h>
#include <stdio.h>

int main(void) {
 @autoreleasepool {
  NSApplication *app=[NSApplication sharedApplication];
  __block unsigned callbacks=0;
  unsigned events=0, latePolls=0;
  uint64_t started=mach_absolute_time();
  for(unsigned i=0;i<200;++i) {
   @autoreleasepool {
    dispatch_async(dispatch_get_main_queue(), ^{++callbacks;});
    NSEvent *event=[NSEvent keyEventWithType:NSKeyDown location:NSZeroPoint
      modifierFlags:0 timestamp:i windowNumber:0 context:nil
      characters:@"probe" charactersIgnoringModifiers:@"probe"
      isARepeat:NO keyCode:0];
    [app postEvent:event atStart:NO];
    NSEvent *received=[app nextEventMatchingMask:NSKeyDownMask
      untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES];
    if(received==event)++events;
    if(callbacks != i+1)++latePolls;
   }
  }
  mach_timebase_info_data_t scale;mach_timebase_info(&scale);
  double elapsed=(mach_absolute_time()-started)*(double)scale.numer/scale.denom/1e6;
  printf("APPKIT_PUMP events=%u callbacks=%u late_polls=%u elapsed_ms=%.3f\n",events,callbacks,latePolls,elapsed);
  return events==200&&callbacks==200&&latePolls==0?0:1;
 }
}
