/* hw.cacheconfig / hw.cachesize as macOS lays them out: 10 uint64 entries. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>

static int dump(const char* name)
{
	uint64_t v[10];
	size_t probed = 0, got = sizeof(v);
	int probe_rv = sysctlbyname(name, NULL, &probed, NULL, 0);
	memset(v, 0, sizeof(v));
	int rv = sysctlbyname(name, v, &got, NULL, 0);
	printf("SYSCTL_CACHE name=%s probe_rv=%d probed=%zu rv=%d got=%zu [", name, probe_rv, probed, rv, got);
	for (int i = 0; i < 5; i++)
		printf("%s%llu", i ? " " : "", (unsigned long long)v[i]);
	int ok = probe_rv == 0 && rv == 0 && probed == sizeof(v) && got == sizeof(v) && v[0] != 0;
	printf("] %s\n", ok ? "ok" : "FAIL");
	return !ok;
}

int main(void)
{
	return dump("hw.cacheconfig") | dump("hw.cachesize");
}
