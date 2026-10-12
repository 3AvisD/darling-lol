/* Probe-then-fetch string sysctls the way callers size a stack buffer. */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>

static int check(const char* name)
{
	size_t probed = (size_t)-1;
	int rv = sysctlbyname(name, NULL, &probed, NULL, 0);
	if (rv != 0 || probed == (size_t)-1 || probed == 0 || probed > 256) {
		printf("SYSCTL_STRING name=%s probe_rv=%d errno=%d len=%zu FAIL\n", name, rv, errno, probed);
		return 1;
	}
	char value[probed];
	size_t got = probed;
	memset(value, 'X', probed);
	rv = sysctlbyname(name, value, &got, NULL, 0);
	int ok = rv == 0 && got == probed && value[probed - 1] == 0 && strlen(value) == probed - 1;
	printf("SYSCTL_STRING name=%s len=%zu value=%.*s %s\n", name, got, (int)probed, value, ok ? "ok" : "FAIL");
	return !ok;
}

int main(void)
{
	int failed = check("hw.model") | check("hw.machine") | check("kern.ostype") | check("kern.osrelease");
	int mib[2] = { CTL_HW, HW_MODEL };
	char model[64];
	size_t length = sizeof(model);
	int by_mib = sysctl(mib, 2, model, &length, NULL, 0) == 0 && length == strlen(model) + 1;
	size_t frequency_length = 0;
	int frequency = sysctlbyname("hw.cpufrequency", NULL, &frequency_length, NULL, 0);
	int missing = sysctlbyname("hw.riot_check_missing", NULL, &length, NULL, 0) == -1 && errno == ENOENT;
	printf("SYSCTL_STRING_RESULT strings=%d mib=%d frequency_probe=%d missing=%d\n",
	       !failed, by_mib, frequency == 0 && frequency_length == sizeof(int), missing);
	return failed || !by_mib || !missing;
}
