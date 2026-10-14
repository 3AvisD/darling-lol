/*
 * Host-side benchmark of the libplatform primitives against the host's glibc.
 *
 * The `current` copy is the code the installed libsystem_platform.dylib was
 * built from (memmove.c/bzero.c/memset_pattern.c at -O2 -fno-builtin, the rest
 * at -O0) and the `new` copy is what tools/build-riot-platform-strings.py
 * produces (-O2 for every string source, plus the vector paths). Both are the
 * same sources as the guest library; glibc is measured in the same process for
 * scale, and is not what the guest runs.
 *
 * Sizes 1..256, then 512, 1 KiB, 4 KiB, 16 KiB, 64 KiB and 1 MiB; the source
 * and destination advance by 32 bytes per iteration so every alignment mod 64
 * is exercised. Every call is a direct call through a volatile function
 * pointer and every result feeds a volatile sink, so nothing is folded away.
 * This is a throughput benchmark, not an FPS claim.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

uint64_t platform_test_capabilities;

#define DECLARE_VARIANT(prefix) \
	extern void *prefix##_platform_memmove(void *, const void *, size_t); \
	extern void *prefix##_platform_memset(void *, int, size_t); \
	extern int prefix##_platform_memcmp(const void *, const void *, size_t); \
	extern void *prefix##_platform_memchr(const void *, int, size_t); \
	extern size_t prefix##_platform_strlen(const char *); \
	extern int prefix##_platform_strcmp(const char *, const char *);

DECLARE_VARIANT(current)
DECLARE_VARIANT(new)

static volatile size_t sink;
static volatile size_t g_size;
static volatile unsigned g_iterations;

static double now_ns(void)
{
	struct timespec time;

	clock_gettime(CLOCK_MONOTONIC, &time);
	return (double)time.tv_sec * 1e9 + (double)time.tv_nsec;
}

static unsigned iterations_for(size_t size)
{
	size_t iterations = 4000000 / (size + 16);

	if (iterations < 100)
		iterations = 100;
	if (iterations > 200000)
		iterations = 200000;
	return (unsigned)iterations;
}

static char *source;
static char *destination;
static size_t capacity = 4u << 20;

enum operation { MOVE, FILL, COMPARE, SEARCH, LENGTH, STRINGCOMPARE };
static const char *operation_names[] = { "memcpy", "memset", "memcmp", "memchr", "strlen", "strcmp" };

typedef void (*operation_function)(int which);

static void run_current(int which)
{
	size_t size = g_size;
	unsigned iterations = g_iterations;
	unsigned index;

	for (index = 0; index < iterations; index++) {
		size_t offset = (index * 32) & 4095;
		size_t i = (index & 1) ? offset : 0;

		switch (which) {
		case MOVE: sink = (size_t)current_platform_memmove(destination + i, source + i, size); break;
		case FILL: sink = (size_t)current_platform_memset(destination + i, 0x5a, size); break;
		case COMPARE: sink = (size_t)current_platform_memcmp(source + i, destination + i, size); break;
		case SEARCH: sink = (size_t)current_platform_memchr(source + i, 0xa5, size); break;
		case LENGTH: sink = current_platform_strlen(source + i); break;
		default: sink = (size_t)current_platform_strcmp(source + i, destination + i); break;
		}
	}
}

static void run_new(int which)
{
	size_t size = g_size;
	unsigned iterations = g_iterations;
	unsigned index;

	for (index = 0; index < iterations; index++) {
		size_t offset = (index * 32) & 4095;
		size_t i = (index & 1) ? offset : 0;

		switch (which) {
		case MOVE: sink = (size_t)new_platform_memmove(destination + i, source + i, size); break;
		case FILL: sink = (size_t)new_platform_memset(destination + i, 0x5a, size); break;
		case COMPARE: sink = (size_t)new_platform_memcmp(source + i, destination + i, size); break;
		case SEARCH: sink = (size_t)new_platform_memchr(source + i, 0xa5, size); break;
		case LENGTH: sink = new_platform_strlen(source + i); break;
		default: sink = (size_t)new_platform_strcmp(source + i, destination + i); break;
		}
	}
}

static void run_glibc(int which)
{
	size_t size = g_size;
	unsigned iterations = g_iterations;
	unsigned index;

	for (index = 0; index < iterations; index++) {
		size_t offset = (index * 32) & 4095;
		size_t i = (index & 1) ? offset : 0;

		switch (which) {
		case MOVE: sink = (size_t)memcpy(destination + i, source + i, size); break;
		case FILL: sink = (size_t)memset(destination + i, 0x5a, size); break;
		case COMPARE: sink = (size_t)memcmp(source + i, destination + i, size); break;
		case SEARCH: sink = (size_t)memchr(source + i, 0xa5, size); break;
		case LENGTH: sink = strlen(source + i); break;
		default: sink = (size_t)strcmp(source + i, destination + i); break;
		}
	}
}

static double measure(operation_function function, int which)
{
	double best = 1e30;
	int round;

	for (round = 0; round < 3; round++) {
		double start, elapsed;

		start = now_ns();
		function(which);
		elapsed = now_ns() - start;
		if (elapsed < best)
			best = elapsed;
	}
	return best / (double)g_iterations;
}

static void prepare_data(int which, size_t size)
{
	size_t boundary;

	switch (which) {
	case COMPARE:
		/* a difference at the end, so a compare scans the whole range */
		memcpy(destination, source, capacity + 8192);
		if (size != 0)
			destination[size - 1] = (unsigned char)(source[size - 1] ^ 0x20);
		break;
	case SEARCH:
		/* 0xa5 does not appear in the source */
		memset(source, 'a', capacity + 8192);
		break;
	case LENGTH:
	case STRINGCOMPARE:
		/* the strings end every size+1 bytes, so the offset sweep stays valid */
		memset(source, 'a', capacity + 8192);
		memset(destination, 'a', capacity + 8192);
		for (boundary = size + 1; boundary < capacity; boundary += size + 1) {
			source[boundary] = 0;
			destination[boundary] = 0;
		}
		break;
	default:
		break;
	}
}

static void measure_size(size_t size)
{
	int which;

	g_size = size;
	g_iterations = iterations_for(size);
	memset(source, 'a', capacity + 8192);
	memset(destination, 'a', capacity + 8192);
	for (which = 0; which < 6; which++) {
		double current, updated, glibc;

		prepare_data(which, size);
		current = measure(run_current, which);
		prepare_data(which, size);
		updated = measure(run_new, which);
		prepare_data(which, size);
		glibc = measure(run_glibc, which);
		printf("BENCH size=%zu op=%s current_ns=%.2f new_ns=%.2f glibc_ns=%.2f current_vs_glibc=%.2f new_vs_glibc=%.2f\n",
		       size, operation_names[which], current, updated, glibc,
		       current / glibc, updated / glibc);
	}
}

int main(int argc, char **argv)
{
	size_t size;
	int single = 0;

	if (argc > 1 && strcmp(argv[1], "--no-avx2") == 0) {
		platform_test_capabilities = 0;
		single = 1;
	} else {
		platform_test_capabilities = 0x20000000 | 0x08000000;
	}
	(void)single;

	source = malloc(capacity + 8192);
	destination = malloc(capacity + 8192);
	if (source == NULL || destination == NULL)
		return 1;
	memset(source, 'a', capacity + 8192);
	memset(destination, 'a', capacity + 8192);

	for (size = 1; size <= 256; size++)
		measure_size(size);
	measure_size(512);
	measure_size(1024);
	measure_size(4096);
	measure_size(16384);
	measure_size(65536);
	measure_size(1048576);
	return 0;
}
