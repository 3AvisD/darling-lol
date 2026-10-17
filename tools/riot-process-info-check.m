#import <Foundation/Foundation.h>
#import <CoreFoundation/CFPriv.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/sysctl.h>

// Numeric version queries must not depend on an overridable, localized string.
@interface LocalizedProcessInfo : NSProcessInfo
@end
@implementation LocalizedProcessInfo
- (NSString *)operatingSystemVersionString { return @"Localized operating system description"; }
@end

int main(void) {
    @autoreleasepool {
        NSDictionary *dictionary = (NSDictionary *)_CFCopySystemVersionDictionary();
        NSString *expected = [dictionary objectForKey:@"FullVersionString"];
        if (![expected length]) expected = [dictionary objectForKey:@"ProductVersion"];
        if (!expected) expected = @"Unknown";
        NSProcessInfo *process = [NSProcessInfo processInfo];
        if (![[process operatingSystemVersionString] isEqualToString:expected]) {
            fprintf(stderr, "FAIL: process version string omits the system version/build description\n");
            return 1;
        }
        NSOperatingSystemVersion version = [process operatingSystemVersion];
        NSOperatingSystemVersion localized = [[[LocalizedProcessInfo alloc] init] operatingSystemVersion];
        if (version.majorVersion != localized.majorVersion ||
            version.minorVersion != localized.minorVersion || version.patchVersion != localized.patchVersion) {
            fprintf(stderr, "FAIL: numeric version depends on the localized description\n");
            return 2;
        }
        uint64_t memory = 0;
        size_t length = sizeof(memory);
        if (sysctlbyname("hw.memsize", &memory, &length, NULL, 0) || memory == 0 ||
            [process physicalMemory] != memory) {
            fprintf(stderr, "FAIL: physicalMemory differs from 64-bit hw.memsize\n");
            return 3;
        }
        [dictionary release];
        printf("PROCESS_INFO version_build=1 localized_numeric=1 memory64=%llu pass=1\n",
               (unsigned long long)memory);
    }
    return 0;
}
