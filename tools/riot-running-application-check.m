#import <AppKit/AppKit.h>
#import <AppKit/NSRunningApplication.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

int main(int argc, const char **argv) {
    @autoreleasepool {
        if (argc == 2 && strcmp(argv[1], "--no-display") == 0) {
            NSRunningApplication *running =
                [NSRunningApplication runningApplicationWithProcessIdentifier:getpid()];
            BOOL failedCleanly = running != nil && ![running activateWithOptions:2];
            printf("RUNNING_APP no_display_failed_cleanly=%d\n", failedCleanly);
            return failedCleanly ? 0 : 1;
        }
        [NSApplication sharedApplication];
        NSWindow *window = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(40, 40, 160, 90)
                      styleMask:NSTitledWindowMask
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [window makeKeyAndOrderFront:nil];

        pid_t ownPID = getpid();
        NSRunningApplication *running =
            [NSRunningApplication runningApplicationWithProcessIdentifier:ownPID];
        BOOL found = running != nil && [running processIdentifier] == ownPID &&
                     ![running isTerminated];
        BOOL activated = [running activateWithOptions:2];
        BOOL missing = [NSRunningApplication
            runningApplicationWithProcessIdentifier:2147483647] == nil;

        printf("RUNNING_APP found=%d activated=%d missing=%d\n",
               found, activated, missing);
        [window close];
        [window release];
        return found && activated && missing ? 0 : 1;
    }
}
