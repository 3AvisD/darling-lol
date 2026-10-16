#include <CoreFoundation/CoreFoundation.h>
#include <CFNetwork/CFHost.h>
#include <stdio.h>

static unsigned callbacks;
static void completed(CFHostRef host, CFHostInfoType type,
                      const CFStreamError *error, void *info) {
    if (host && type == kCFHostAddresses && info == &callbacks)
        ++callbacks;
}

int main(void) {
    CFHostClientContext context = {0, &callbacks, NULL, NULL, NULL};
    for (unsigned i = 0; i < 100; ++i) {
        CFHostRef host = CFHostCreateWithName(NULL, CFSTR("localhost"));
        CFStreamError error = {0};
        if (!host || !CFHostSetClient(host, completed, &context)) return 1;
        CFHostScheduleWithRunLoop(host, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        if (!CFHostStartInfoResolution(host, kCFHostAddresses, &error)) return 2;
        CFHostCancelInfoResolution(host, kCFHostAddresses);
        /* The cancel source must keep its host alive until delivery. */
        if (i % 2) CFRelease(host);
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
        if (callbacks != i + 1) return 3;
        if (!(i % 2)) CFRelease(host);
    }
    puts("CFHOST_CANCEL_CHECK cancel callback lifetime repeated pass=1");
    return 0;
}
