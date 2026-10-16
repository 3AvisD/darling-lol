#include <CoreFoundation/CoreFoundation.h>
#include <CFNetwork/CFHost.h>
#include <stdio.h>

static unsigned callbacks, unexpected;
static void completed(CFHostRef host, CFHostInfoType type,
                      const CFStreamError *error, void *info) {
    /* A cancellation stores no addresses; a completed lookup always does. */
    Boolean resolved = false;
    if (host) CFHostGetAddressing(host, &resolved);
    if (host && type == kCFHostAddresses && info == &callbacks && !resolved)
        ++callbacks;
    else
        ++unexpected;
}

static int fail(const char *stage, unsigned i) {
    printf("CFHOST_CANCEL_CHECK fail stage=%s iteration=%u callbacks=%u unexpected=%u\n",
           stage, i, callbacks, unexpected);
    return 1;
}

int main(void) {
    CFHostClientContext context = {0, &callbacks, NULL, NULL, NULL};
    for (unsigned i = 0; i < 100; ++i) {
        CFHostRef host = CFHostCreateWithName(NULL, CFSTR("localhost"));
        CFStreamError error = {0};
        if (!host || !CFHostSetClient(host, completed, &context)) return fail("client", i);
        CFHostScheduleWithRunLoop(host, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        if (!CFHostStartInfoResolution(host, kCFHostAddresses, &error)) return fail("start", i);
        CFHostCancelInfoResolution(host, kCFHostAddresses);
        /* The cancel source must keep its host alive until delivery. */
        if (i % 2) CFRelease(host);
        CFAbsoluteTime deadline = CFAbsoluteTimeGetCurrent() + 2.0;
        while (callbacks != i + 1 && !unexpected && CFAbsoluteTimeGetCurrent() < deadline)
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
        if (unexpected) return fail("unexpected-callback", i);
        if (callbacks != i + 1) return fail("cancel-callback", i);
        if (!(i % 2)) CFRelease(host);
    }
    puts("CFHOST_CANCEL_CHECK cancel callback lifetime repeated pass=1");
    return 0;
}
