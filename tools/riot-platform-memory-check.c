/*
 * Host-side test for the libplatform string primitives the guest uses.
 *
 * Two copies of the same sources are linked in:
 *
 *   current - the flags the installed libsystem_platform.dylib was built with
 *             (memmove.c/bzero.c/memset_pattern.c at -O2 -fno-builtin, the rest
 *             at -O0);
 *   new     - the flags tools/build-riot-platform-strings.py uses (-O2
 *             -fno-builtin -fno-strict-aliasing for every string source).
 *
 * Both copies read one capability word, so this binary covers the generic
 * path, the vector path and the difference between them: every case runs
 * against both copies and against a byte-loop reference, and the guarded
 * regions turn any access outside [buffer, buffer+size) into a test failure.
 *
 * The flags of each copy are the build script's, not this file's; see
 * tools/build-riot-platform-memory-check.py. Host only: no Darling process,
 * prefix or guest file is touched.
 */
#include <stddef.h>
#include <signal.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

uint64_t platform_test_capabilities;	/* the shim's commpage word */

#define DECLARE_VARIANT(prefix) \
	extern void *prefix##_platform_memmove(void *, const void *, size_t); \
	extern void *prefix##_platform_memset(void *, int, size_t); \
	extern void prefix##_platform_bzero(void *, size_t); \
	extern void prefix##_platform_memset_pattern16(void *, const void *, size_t); \
	extern int prefix##_platform_memcmp(const void *, const void *, size_t); \
	extern void *prefix##_platform_memchr(const void *, int, size_t); \
	extern size_t prefix##_platform_strlen(const char *); \
	extern int prefix##_platform_strcmp(const char *, const char *);

DECLARE_VARIANT(current)
DECLARE_VARIANT(new)

struct primitives {
	const char *name;
	void *(*memmove)(void *, const void *, size_t);
	void *(*memset_)(void *, int, size_t);
	void (*bzero)(void *, size_t);
	void (*memset_pattern16)(void *, const void *, size_t);
	int (*memcmp_)(const void *, const void *, size_t);
	void *(*memchr)(const void *, int, size_t);
	size_t (*strlen_)(const char *);
	int (*strcmp_)(const char *, const char *);
};

#define VARIANT(prefix) \
	{ #prefix, prefix##_platform_memmove, prefix##_platform_memset, prefix##_platform_bzero, \
	  prefix##_platform_memset_pattern16, prefix##_platform_memcmp, prefix##_platform_memchr, \
	  prefix##_platform_strlen, prefix##_platform_strcmp }

static const struct primitives variants[] = { VARIANT(current), VARIANT(new) };

#define VARIANT_COUNT (sizeof(variants) / sizeof(variants[0]))

#define MARKER 0x5a

static size_t page_size;
static unsigned char *guard_left;	/* first mapped byte */
static unsigned char *guard_right;	/* one past the last mapped byte */
static size_t guarded_capacity;

static sigjmp_buf fault_jump;
static volatile int fault_armed;
static unsigned long fault_count;

static unsigned long failures;
static unsigned long cases;

static unsigned char reference_source[1 << 16];
static unsigned char reference_result[1 << 16];

static void fault_handler(int signal)
{
	(void)signal;
	if (fault_armed) {
		fault_count++;
		siglongjmp(fault_jump, 1);
	}
	_exit(2);
}

static void fail(const struct primitives *variant, const char *what, const char *detail,
                 size_t a, size_t b)
{
	fprintf(stderr, "FAIL %s %s %s a=%zu b=%zu\n", variant->name, what, detail, a, b);
	if (++failures > 20)
		exit(3);
}

static void fill_pattern(unsigned char *buffer, size_t size, unsigned seed)
{
	size_t index;
	uint32_t state = seed * 2654435761u + 1;

	for (index = 0; index < size; index++) {
		state ^= state << 13; state ^= state >> 17; state ^= state << 5;
		buffer[index] = (unsigned char)(state >> 8);
	}
}

/* Returns the number of differences between the destination and the bytes a
 * practical memmove leaves there. */
static unsigned long move_mismatches(const unsigned char *destination, size_t size)
{
	size_t index;

	for (index = 0; index < size; index++) {
		if (destination[index] != reference_result[index])
			return size - index;
	}
	return 0;
}

/*
 * One memmove inside the guarded region. The caller has already placed the
 * source bytes and filled the rest of the region with MARKER; this snapshots
 * the whole region, computes what a byte loop would leave there, calls the
 * implementation and compares the whole region, so a write outside
 * [destination, destination+size) or into the source shows up as a difference
 * wherever it lands.
 */
static void check_move(const struct primitives *variant, unsigned char *destination,
                       const unsigned char *source, size_t size, const char *detail,
                       size_t a, size_t b)
{
	const ptrdiff_t destination_offset = destination - guard_left;
	const ptrdiff_t source_offset = source - guard_left;
	size_t index;

	memcpy(reference_source, guard_left, guarded_capacity);
	memcpy(reference_result, reference_source, guarded_capacity);
	/* from the snapshot, so the reference has memmove semantics whatever the
	   direction of the overlap */
	for (index = 0; index < size; index++)
		reference_result[destination_offset + index] = reference_source[source_offset + index];
	cases++;
	if (variant->memmove(destination, source, size) != destination) {
		fail(variant, "memmove", "return value", size, a);
		return;
	}
	for (index = 0; index < guarded_capacity; index++) {
		if (guard_left[index] != reference_result[index]) {
			fail(variant, "memmove", detail, size, a);
			fprintf(stderr, "  size=%zu distance=%zu first difference at %zu (destination %td source %td)\n",
			        size, a, index, destination_offset, source_offset);
			return;
		}
	}
}

/* The same comparison for two heap buffers that may overlap. */
static void check_move_plain(const struct primitives *variant, unsigned char *destination,
                             const unsigned char *source, size_t size, const char *detail,
                             size_t a)
{
	size_t index;
	int overlapping = !((const unsigned char *)destination + size <= source ||
	                    source + size <= (const unsigned char *)destination);

	memcpy(reference_source, source, size);
	memcpy(reference_result, reference_source, size);
	if (!overlapping)
		memset(destination, MARKER, size);
	cases++;
	if (variant->memmove(destination, source, size) != destination) {
		fail(variant, "memmove", "return value", size, a);
		return;
	}
	if (move_mismatches(destination, size)) {
		fail(variant, "memmove", detail, size, a);
		return;
	}
	if (!overlapping) {
		for (index = size; index < size + 64; index++) {
			if (destination[index] != MARKER) {
				fail(variant, "memmove", "wrote past destination", size, a);
				return;
			}
		}
	}
}

/* memmove/memcpy between buffers with an unmapped page on each side. */
static void test_move_guarded(const struct primitives *variant)
{
	static const size_t interesting[] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17, 23, 24, 25, 31, 32, 33,
		47, 48, 49, 63, 64, 65, 79, 95, 96, 97, 127, 128, 129, 191, 255, 256,
		257, 383, 511, 512, 513, 1023, 1024, 1025, 2047, 2048, 2049, 4095, 4096, 4097,
		8191, 8192, 8193,
	};
	static const size_t deltas[] = { 0, 1, 2, 3, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 65, 129 };
	size_t index, shift, delta;

	for (index = 0; index < sizeof(interesting) / sizeof(interesting[0]); index++) {
		size_t size = interesting[index];

		if (size + 64 >= guarded_capacity / 2)
			continue;
		for (shift = 1; shift < 32; shift += 7) {
			/*
			 * Source against the upper page, destination against the
			 * lower one, and the other way round; shift moves the buffer
			 * away from its guard page by up to 31 bytes, so every buffer
			 * keeps a byte of mapped space on each side of it.
			 */
			unsigned char *upper = guard_right - shift - size;
			unsigned char *lower = guard_left + shift;

			memset(guard_left, MARKER, guarded_capacity);
			fill_pattern(upper, size, (unsigned)(size + shift));
			check_move(variant, lower, upper, size, "guarded destination low", shift, size);
			memset(guard_left, MARKER, guarded_capacity);
			fill_pattern(lower, size, (unsigned)(size + shift));
			check_move(variant, upper, lower, size, "guarded destination high", shift, size);

			for (delta = 0; delta < sizeof(deltas) / sizeof(deltas[0]); delta++) {
				size_t distance = deltas[delta];

				if (size + distance + shift >= guarded_capacity / 2)
					continue;
				memset(guard_left, MARKER, guarded_capacity);
				{
					unsigned char *source = guard_left + shift + distance;
					unsigned char *destination = guard_left + shift;

					fill_pattern(source, size, (unsigned)(size + shift + distance));
					check_move(variant, destination, source, size,
					           "overlap destination below", distance, size);
				}
				memset(guard_left, MARKER, guarded_capacity);
				{
					unsigned char *source = guard_left + shift;
					unsigned char *destination = guard_left + shift + distance;

					fill_pattern(source, size, (unsigned)(size + shift));
					check_move(variant, destination, source, size,
					           "overlap destination above", distance, size);
				}
			}
		}
	}
}

/* A destination that only just fits the guarded region, so both sides are
 * unmapped at once. */
static void test_move_both_guards(const struct primitives *variant)
{
	size_t size = guarded_capacity - 128;
	size_t index;

	memset(guard_left, MARKER, guarded_capacity);
	fill_pattern(guard_left + 64, size, 99);
	for (index = 0; index + 64 <= size; index += 509) {
		check_move(variant, guard_left + 64, guard_left + 64 + index, size - index,
		           "both guards", index, size);
	}
}

static void test_move_alignments(const struct primitives *variant, size_t limit)
{
	static const unsigned alignments[][2] = {
		{ 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 }, { 3, 5 }, { 7, 0 }, { 0, 7 },
		{ 15, 1 }, { 1, 15 }, { 31, 0 }, { 0, 31 }, { 33, 63 }, { 63, 33 },
		{ 63, 63 }, { 16, 48 }, { 48, 16 },
	};
	size_t size, pair;
	unsigned char *destination = malloc(limit + 256);
	unsigned char *source = malloc(limit + 256);

	for (size = 0; size <= limit; size++) {
		for (pair = 0; pair < sizeof(alignments) / sizeof(alignments[0]); pair++) {
			unsigned char *d = destination + 64 + alignments[pair][0];
			unsigned char *s = source + 64 + alignments[pair][1];

			fill_pattern(source, limit + 256, (unsigned)(size * 131 + pair));
			memset(d, MARKER, limit + 128);
			check_move_plain(variant, d, s, size, "alignment", pair);
		}
	}
	free(destination);
	free(source);
}

/* Random sizes, alignments and overlap distances. */
static void test_move_random(const struct primitives *variant, unsigned long rounds)
{
	const size_t capacity = 1 << 16;
	unsigned char *destination = malloc(capacity + 128);
	unsigned char *source = malloc(capacity + 128);
	uint32_t state = 12345;
	unsigned long round;

	for (round = 0; round < rounds; round++) {
		size_t size, destination_offset, source_offset;
		unsigned char *d, *s;

		state ^= state << 13; state ^= state >> 17; state ^= state << 5;
		size = state % 4096;
		if ((state & 0x3f) == 0) {
			state ^= state << 13; state ^= state >> 17; state ^= state << 5;
			size = state % 16384;
		}
		destination_offset = 1 + state % 63;
		source_offset = 1 + state % 63;
		fill_pattern(source, capacity + 128, state);
		memset(destination, MARKER, capacity + 128);
		d = destination + destination_offset;
		s = source + source_offset;
		check_move_plain(variant, d, s, size, "random", round);
	}
	free(destination);
	free(source);
}

/* The same, with the destination inside the source or the source inside the
 * destination, which is the case a memmove must get right. */
static void test_move_overlap_random(const struct primitives *variant, unsigned long rounds)
{
	const size_t capacity = 1 << 16;
	unsigned char *buffer = malloc(capacity + 128);
	uint32_t state = 987654321;
	unsigned long round;

	for (round = 0; round < rounds; round++) {
		size_t size, offset, delta;
		unsigned char *s, *d;

		/* both ranges stay inside the buffer, so 2*size must fit */
		state ^= state << 13; state ^= state >> 17; state ^= state << 5;
		size = 1 + state % 4096;
		offset = 1 + state % (capacity - 2 * size - 2);
		delta = state % size;
		fill_pattern(buffer, capacity + 128, state);
		s = buffer + offset;
		d = buffer + offset + delta;
		check_move_plain(variant, d, s, size, "overlap above", delta);

		state ^= state << 13; state ^= state >> 17; state ^= state << 5;
		size = 1 + state % 4096;
		offset = 1 + state % (capacity - 2 * size - 2);
		delta = state % size;
		fill_pattern(buffer, capacity + 128, state);
		s = buffer + offset + delta;
		d = buffer + offset;
		check_move_plain(variant, d, s, size, "overlap below", delta);
	}
	/* A copy onto itself must change nothing. */
	fill_pattern(buffer, 4096, 5);
	memcpy(reference_result, buffer, 4096);
	cases++;
	variant->memmove(buffer, buffer, 4096);
	if (move_mismatches(buffer, 4096))
		fail(variant, "memmove", "self copy", 4096, 0);
	free(buffer);
}

static void test_fill_guarded(const struct primitives *variant)
{
	static const size_t interesting[] = {
		0, 1, 2, 3, 4, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129,
		255, 256, 257, 511, 512, 513, 1023, 1024, 2048, 4095, 4096, 4097, 8192,
	};
	static const int values[] = { 0, 0x5a, 0xff, 0x100 + 0x37 };
	size_t index, shift, value_index;

	for (index = 0; index < sizeof(interesting) / sizeof(interesting[0]); index++) {
		size_t size = interesting[index];

		if (size + 1 >= guarded_capacity / 2)
			continue;
		for (shift = 1; shift < 32; shift += 11) {
			for (value_index = 0; value_index < sizeof(values) / sizeof(values[0]); value_index++) {
				int value = values[value_index];
				unsigned char *d = guard_right - shift - size;
				size_t position;

				memset(guard_left, MARKER, guarded_capacity);
				cases++;
				if (variant->memset_(d, value, size) != d)
					fail(variant, "memset", "return value", size, (size_t)value);
				for (position = 0; position < size; position++) {
					if (d[position] != (unsigned char)value) {
						fail(variant, "memset", "byte", size, position);
						break;
					}
				}
				if (d[-1] != MARKER || d[size] != MARKER)
					fail(variant, "memset", "wrote outside", size, (size_t)value);

				d = guard_left + shift;
				memset(guard_left, MARKER, guarded_capacity);
				cases++;
				variant->bzero(d, size);
				for (position = 0; position < size; position++) {
					if (d[position] != 0) {
						fail(variant, "bzero", "byte", size, position);
						break;
					}
				}
				if (d[-1] != MARKER || d[size] != MARKER)
					fail(variant, "bzero", "wrote outside", size, 0);
			}
		}
	}
	for (index = 0; index < sizeof(interesting) / sizeof(interesting[0]); index++) {
		size_t size = interesting[index];
		unsigned char pattern[16];
		unsigned char *d;
		size_t position;

		if (size + 1 >= guarded_capacity)
			continue;
		d = guard_right - size - 1;
		memset(guard_left, MARKER, guarded_capacity);
		fill_pattern(pattern, sizeof(pattern), 3);
		cases++;
		variant->memset_pattern16(d, pattern, size);
		for (position = 0; position < size; position++) {
			if (d[position] != pattern[position & 15]) {
				fail(variant, "memset_pattern16", "byte", size, position);
				break;
			}
		}
	}
}

static void test_compare(const struct primitives *variant)
{
	static const size_t lengths[] = { 0, 1, 2, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129, 255, 256, 257, 1024, 4096, 4097 };
	unsigned char *a = malloc(8192);
	unsigned char *b = malloc(8192);
	size_t index, difference, length;

	for (index = 0; index < sizeof(lengths) / sizeof(lengths[0]); index++) {
		length = lengths[index];
		for (difference = 0; difference <= length; difference += (length > 64 ? 37 : 1)) {
			int expected, actual;
			size_t step;

			fill_pattern(a, 8192, (unsigned)length + 1);
			memcpy(b, a, 8192);
			expected = 0;
			if (difference < length) {
				b[difference] ^= 0x80;
				expected = (int)a[difference] - (int)b[difference];
			}
			actual = variant->memcmp_(a, b, length);
			cases++;
			if ((actual == 0) != (expected == 0) || (actual > 0) != (expected > 0))
				fail(variant, "memcmp", "sign", length, difference);
			actual = variant->memcmp_(b, a, length);
			if (actual != -expected)
				fail(variant, "memcmp", "swapped value", length, difference);
			for (step = 0; length != 0 && step < 3; step++) {
				/* A difference after a longer equal prefix. */
				size_t at = (difference + step * 7919) % length;

				memcpy(b, a, 8192);
				b[at] ^= 0x01;
				actual = variant->memcmp_(a, b, length);
				expected = (int)a[at] - (int)b[at];
				if ((actual == 0) != (expected == 0) || (actual > 0) != (expected > 0))
					fail(variant, "memcmp", "second difference", length, at);
			}
		}
	}
	free(a);
	free(b);
}

static void test_search(const struct primitives *variant)
{
	unsigned char *buffer = malloc(4096);
	size_t length, position;

	for (length = 0; length <= 1024; length++) {
		unsigned char *found;
		size_t index;

		for (index = 0; index < 4096; index++)
			buffer[index] = (unsigned char)(index & 1);
		for (position = 0; position <= length; position += (length > 32 ? 13 : 1)) {
			unsigned char needle = 0xa5;

			if (position < length)
				buffer[position] = needle;
			found = variant->memchr(buffer, needle, length);
			cases++;
			if (position < length) {
				if (found == NULL || (size_t)(found - buffer) != position)
					fail(variant, "memchr", "found", length, position);
			} else if (found != NULL) {
				fail(variant, "memchr", "false positive", length, position);
			}
			if (position < length)
				buffer[position] = (unsigned char)(position & 1);
		}
	}
	free(buffer);
}

static void test_strings(const struct primitives *variant)
{
	unsigned char *buffer = malloc(2048);
	size_t length;

	for (length = 0; length <= 1024; length++) {
		size_t index;

		for (index = 0; index < length; index++)
			buffer[index] = (unsigned char)('a' + (index % 26));
		buffer[length] = 0;
		cases++;
		if (variant->strlen_((const char *)buffer) != length)
			fail(variant, "strlen", "length", length, 0);
	}
	for (length = 0; length <= 512; length += 7) {
		size_t difference;

		for (difference = 0; difference <= length; difference += 3) {
			unsigned char a[1024], b[1024];
			size_t index;
			int expected, actual;

			for (index = 0; index < length; index++) {
				/* never zero, so only the intended byte differs */
				a[index] = (unsigned char)(1 + (index * 13) % 255);
				b[index] = a[index];
			}
			a[length] = b[length] = 0;
			expected = 0;
			if (difference < length) {
				b[difference] = (unsigned char)(a[difference] + (difference & 1 ? 1 : 251));
				expected = (int)(unsigned char)a[difference] - (int)(unsigned char)b[difference];
			}
			actual = variant->strcmp_((const char *)a, (const char *)b);
			cases++;
			if ((actual == 0) != (expected == 0) || (actual > 0) != (expected > 0))
				fail(variant, "strcmp", "sign", length, difference);
		}
	}
	free(buffer);
}

/* Strings whose terminator is the last byte of a guarded page. */
static void test_strings_guarded(const struct primitives *variant)
{
	static const size_t lengths[] = { 0, 1, 2, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 255, 256, 511, 512, 1023, 2047 };
	size_t index;

	for (index = 0; index < sizeof(lengths) / sizeof(lengths[0]); index++) {
		size_t size = lengths[index];
		char *string;
		size_t position;

		if (size + 1 >= guarded_capacity)
			continue;
		string = (char *)(guard_right - size - 1);
		for (position = 0; position < size; position++)
			string[position] = (char)('a' + (position % 26));
		string[size] = 0;
		cases++;
		if (variant->strlen_(string) != size)
			fail(variant, "strlen", "guarded", size, 0);
		if (size > 0) {
			char *other = (char *)guard_left;

			for (position = 0; position < size; position++)
				other[position] = string[position];
			other[size] = 0;
			if (variant->strcmp_(string, other) != 0)
				fail(variant, "strcmp", "guarded equal", size, 0);
			/* the last byte differs, so `string` is the smaller one */
			other[size - 1] = (char)(string[size - 1] + 1);
			if (variant->strcmp_(string, other) >= 0)
				fail(variant, "strcmp", "guarded smaller", size, 0);
			if (variant->strcmp_(other, string) <= 0)
				fail(variant, "strcmp", "guarded larger", size, 0);
		}
	}
}

static int run_all(const struct primitives *variant)
{
	unsigned long before = failures;
	int faulted = 0;

	if (sigsetjmp(fault_jump, 1) == 0) {
		fault_armed = 1;
		test_move_guarded(variant);
		test_move_both_guards(variant);
		test_fill_guarded(variant);
		test_strings_guarded(variant);
		fault_armed = 0;
	} else {
		fault_armed = 0;
		fprintf(stderr, "FAIL %s page fault\n", variant->name);
		failures++;
		faulted = 1;
	}
	test_move_alignments(variant, 512);
	test_move_random(variant, 20000);
	test_move_overlap_random(variant, 5000);
	test_compare(variant);
	test_search(variant);
	test_strings(variant);
	return failures == before && !faulted;
}

int main(int argc, char **argv)
{
	struct sigaction action;
	uint64_t capabilities = 0x20000000 | 0x08000000;
	void *region;
	size_t index, pages = 12;
	int ok = 1;

	for (index = 1; index < (size_t)argc; index++) {
		if (strcmp(argv[index], "--no-avx2") == 0)
			capabilities = 0;
		else if (strcmp(argv[index], "--avx2") == 0)
			capabilities = 0x20000000;
		else if (strcmp(argv[index], "--avx2-erms") == 0)
			capabilities = 0x20000000 | 0x08000000;
		else {
			fprintf(stderr, "unknown option %s\n", argv[index]);
			return 1;
		}
	}

	page_size = (size_t)sysconf(_SC_PAGESIZE);
	region = mmap(NULL, page_size * pages, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (region == MAP_FAILED || page_size < 4096) {
		fprintf(stderr, "cannot allocate guarded pages\n");
		return 1;
	}
	if (mprotect(region, page_size, PROT_NONE) != 0 ||
	    mprotect((unsigned char *)region + page_size * (pages - 1), page_size, PROT_NONE) != 0) {
		fprintf(stderr, "cannot protect guarded pages\n");
		return 1;
	}
	guard_left = (unsigned char *)region + page_size;
	guard_right = guard_left + page_size * (pages - 2);
	guarded_capacity = (size_t)(guard_right - guard_left);
	memset(&action, 0, sizeof(action));
	action.sa_handler = fault_handler;
	sigemptyset(&action.sa_mask);
	sigaction(SIGSEGV, &action, NULL);
	sigaction(SIGBUS, &action, NULL);

	for (index = 0; index < VARIANT_COUNT; index++) {
		platform_test_capabilities = capabilities;
		ok = run_all(&variants[index]) && ok;
	}
	printf("PLATFORM_MEMORY_CHECK pass=%d capabilities=%#llx cases=%lu mismatches=%lu page_faults=%lu variants=%zu\n",
	       failures == 0 && ok, (unsigned long long)capabilities, cases, failures, fault_count,
	       (size_t)VARIANT_COUNT);
	return failures == 0 && ok ? 0 : 1;
}
