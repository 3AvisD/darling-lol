#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/sysctl.h>

int main(void) {
    size_t length = 0;
    if (sysctlbyname("hw.cpufrequency", NULL, &length, NULL, 0) || length != sizeof(uint64_t)) return 1;
    const char *paths[] = {
        "/Volumes/SystemRoot/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq",
        "/Volumes/SystemRoot/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq",
        "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq",
        "/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq"
    };
    uint64_t expected = 0;
    for (unsigned i=0; i<4; ++i) {
        FILE* source = fopen(paths[i], "r");
        unsigned long long khz = 0;
        if (!source) continue;
        int found = fscanf(source, "%llu", &khz); fclose(source);
        if (found == 1 && khz > 0 && khz <= UINT64_MAX/1000) { expected = khz * 1000; break; }
    }
    uint64_t hz = 0;
    length = sizeof(hz);
    if (sysctlbyname("hw.cpufrequency", &hz, &length, NULL, 0)) {
        if (errno != ENOENT || expected) return 2;
        puts("CPU_FREQUENCY unavailable=1 invented_frequency=0 pass=1");
        return 0;
    }
    if (length != 8 || hz < 1000000) return 3;
    if (expected && hz != expected) return 4;
    struct { uint32_t before, value, after; } legacy = {0x12345678,0,0x87654321};
    int mib[] = {CTL_HW, HW_CPU_FREQ}; length = sizeof(legacy.value);
    if (sysctl(mib, 2, &legacy.value, &length, NULL, 0) || length != 4 || legacy.value != (uint32_t)hz ||
        legacy.before != 0x12345678 || legacy.after != 0x87654321) return 5;
    printf("CPU_FREQUENCY hz=%llu width64=1 legacy_width32=1 pass=1\n", (unsigned long long)hz);
    return 0;
}
