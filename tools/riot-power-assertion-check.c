/* Standalone guest probe for Chromium's IOPM assertion create/retain/release API.
 * The host integration fixture advances checkpoints and inspects fake inhibitors.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <IOKit/IOReturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL: "); printf(__VA_ARGS__); puts(""); } } while (0)

static void checkpoint(const char* name)
{
    if (failures) { printf("POWER_ASSERTION FAIL failures=%d\n", failures); exit(1); }
    printf("STAGE %s\n", name);
    fflush(stdout);
    if (getchar() == EOF) exit(2);
}

static IOPMAssertionID create(CFStringRef type, IOPMAssertionLevel level)
{
    IOPMAssertionID id = kIOPMNullAssertionID;
    IOReturn result = IOPMAssertionCreateWithName(type, level, CFSTR("Darling assertion regression"), &id);
    CHECK(result == kIOReturnSuccess && id != kIOPMNullAssertionID, "create assertion: 0x%x id=%u", result, id);
    return id;
}

static void release(IOPMAssertionID id)
{
    IOReturn result = IOPMAssertionRelease(id);
    CHECK(result == kIOReturnSuccess, "release assertion %u: 0x%x", id, result);
}

static void level(IOPMAssertionID id, int value)
{
    CFNumberRef number = CFNumberCreate(NULL, kCFNumberIntType, &value);
    IOReturn result = IOPMAssertionSetProperty(id, kIOPMAssertionLevelKey, number);
    CFRelease(number);
    CHECK(result == kIOReturnSuccess, "set assertion level %u to %d: 0x%x", id, value, result);
}

int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc == 2 && !strcmp(argv[1], "--exit-held")) {
        create(kIOPMAssertionTypeNoIdleSleep, kIOPMAssertionLevelOn);
        create(kIOPMAssertionTypeNoDisplaySleep, kIOPMAssertionLevelOn);
        checkpoint("orphan-held");
        _exit(0); /* Deliberately do not release: iokitd must observe process exit. */
    }

    IOPMAssertionID invalid = kIOPMNullAssertionID;
    IOReturn error = IOPMAssertionCreateWithName(CFSTR("UnsupportedRegressionType"),
                         kIOPMAssertionLevelOn, CFSTR("unsupported"), &invalid);
    CHECK(error == kIOReturnUnsupported && invalid == kIOPMNullAssertionID,
          "unsupported type: 0x%x id=%u", error, invalid);

    CFMutableDictionaryRef timed = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                                            &kCFTypeDictionaryValueCallBacks);
    int seconds = 5;
    CFNumberRef timeout = CFNumberCreate(NULL, kCFNumberIntType, &seconds);
    CFDictionarySetValue(timed, kIOPMAssertionTypeKey, kIOPMAssertionTypeNoIdleSleep);
    CFDictionarySetValue(timed, kIOPMAssertionNameKey, CFSTR("unsupported timeout"));
    CFDictionarySetValue(timed, kIOPMAssertionTimeoutKey, timeout);
    invalid = kIOPMNullAssertionID;
    error = IOPMAssertionCreateWithProperties(timed, &invalid);
    CHECK(error == kIOReturnUnsupported && invalid == kIOPMNullAssertionID,
          "unsupported timeout: 0x%x id=%u", error, invalid);
    CFRelease(timeout);
    CFRelease(timed);

    IOPMAssertionID legacy = create(kIOPMAssertionTypeNoIdleSleep, kIOPMAssertionLevelOn);
    IOPMAssertionID modern = create(kIOPMAssertionTypePreventUserIdleSystemSleep, kIOPMAssertionLevelOn);
    CHECK(legacy != modern, "assertion IDs must be distinct");
    IOPMAssertionRetain(legacy);
    release(legacy); /* One retained reference remains. */
    release(modern);
    checkpoint("idle-held");
    release(legacy);
    CHECK(IOPMAssertionRelease(legacy) == kIOReturnNotFound, "double release not rejected");
    checkpoint("idle-released");

    legacy = create(kIOPMAssertionTypeNoDisplaySleep, kIOPMAssertionLevelOn);
    modern = create(kIOPMAssertionTypePreventUserIdleDisplaySleep, kIOPMAssertionLevelOn);
    release(legacy);
    checkpoint("display-held");
    level(modern, kIOPMAssertionLevelOff);
    checkpoint("display-off");
    level(modern, kIOPMAssertionLevelOn);
    checkpoint("display-on");
    release(modern);
    checkpoint("display-released");

    legacy = create(kIOPMAssertionTypeNoIdleSleep, kIOPMAssertionLevelOff);
    checkpoint("idle-off");
    level(legacy, kIOPMAssertionLevelOn);
    checkpoint("idle-on");
    release(legacy);
    checkpoint("finished");
    printf("POWER_ASSERTION %s failures=%d\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
