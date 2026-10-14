/* Host-build shim standing in for Darling's private/platform/string.h.
 * The only content the string sources need from it is the
 * _PLATFORM_OPTIMIZED_* selection macros, all 0 in the installed build. */
#ifndef PLATFORM_STRING_SHIM_H
#define PLATFORM_STRING_SHIM_H
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define _PLATFORM_OPTIMIZED_BZERO 0
#define _PLATFORM_OPTIMIZED_MEMCCPY 0
#define _PLATFORM_OPTIMIZED_MEMCHR 0
#define _PLATFORM_OPTIMIZED_MEMCMP 0
#define _PLATFORM_OPTIMIZED_MEMMOVE 0
#define _PLATFORM_OPTIMIZED_MEMSET 0
#define _PLATFORM_OPTIMIZED_MEMSET_PATTERN4 0
#define _PLATFORM_OPTIMIZED_MEMSET_PATTERN8 0
#define _PLATFORM_OPTIMIZED_MEMSET_PATTERN16 0
#define _PLATFORM_OPTIMIZED_STRCHR 0
#define _PLATFORM_OPTIMIZED_STRCMP 0
#define _PLATFORM_OPTIMIZED_STRCPY 0
#define _PLATFORM_OPTIMIZED_STRLCAT 0
#define _PLATFORM_OPTIMIZED_STRLCPY 0
#define _PLATFORM_OPTIMIZED_STRLEN 0
#define _PLATFORM_OPTIMIZED_STRNCMP 0
#define _PLATFORM_OPTIMIZED_STRNCPY 0
#define _PLATFORM_OPTIMIZED_STRNLEN 0
#define _PLATFORM_OPTIMIZED_STRSTR 0

/* The host test builds the non-static variant, as the guest dylib does. */
#define VARIANT_STATIC 0

#define __FBSDID(x)

extern void *_platform_memmove(void *, const void *, size_t);
extern void *_platform_memset(void *, int, size_t);
extern void _platform_bzero(void *, size_t);
extern void _platform_memset_pattern4(void *, const void *, size_t);
extern void _platform_memset_pattern8(void *, const void *, size_t);
extern void _platform_memset_pattern16(void *, const void *, size_t);
extern size_t _platform_strlen(const char *);
extern int _platform_strcmp(const char *, const char *);
extern int _platform_memcmp(const void *, const void *, size_t);
extern void *_platform_memchr(const void *, int, size_t);

#endif
