#include <CarbonCore/MacErrors.h>
#include <CarbonCore/Gestalt.h>
#include <sys/sysctl.h>
#include <stdio.h>

int main() {
    char product[64] = {};
    size_t size = sizeof(product);
    int expectedMajor = 0, expectedMinor = 0, expectedPatch = 0;
    if (sysctlbyname("kern.osproductversion", product, &size, nullptr, 0) ||
        sscanf(product, "%d.%d.%d", &expectedMajor, &expectedMinor, &expectedPatch) < 1)
        return 2;
    int major = -1, minor = -1, patch = -1, packed = -1, architecture = -1;
    bool pass = !Gestalt(gestaltSystemVersionMajor, &major) && major == expectedMajor &&
        !Gestalt(gestaltSystemVersionMinor, &minor) && minor == expectedMinor &&
        !Gestalt(gestaltSystemVersionPatch, &patch) && patch == expectedPatch;
    int expectedPacked = ((((expectedMajor / 10) << 4) | (expectedMajor % 10)) << 8) |
        ((expectedMinor > 9 ? 9 : expectedMinor) << 4) | (expectedPatch > 9 ? 9 : expectedPatch);
    pass &= !Gestalt(gestaltSystemVersion, &packed) && packed == expectedPacked;
    pass &= !Gestalt(gestaltSysArchitecture, &architecture) && architecture == gestaltIntel;
    pass &= Gestalt(gestaltSystemVersionMajor, nullptr) == paramErr;
    int unknown = 123;
    pass &= Gestalt(0, &unknown) == unimpErr && unknown == 0;
    printf("GESTALT_VERSION product=%s version=%d.%d.%d packed=0x%x architecture=%d pass=%d\n",
        product, major, minor, patch, packed, architecture, pass);
    return pass ? 0 : 1;
}
