/* Host-build shim standing in for the kernel's i386/cpu_capabilities.h.
 *
 * The guest reads the capability word from the fixed commpage address that
 * mldr maps. The host test cannot rely on that address being free, so the
 * address is redirected to a variable the test owns; everything about how the
 * sources read the word stays the same. The capability bits are the kernel's
 * values (osfmk/i386/cpu_capabilities.h). */
#ifndef I386_CPU_CAPABILITIES_SHIM_H
#define I386_CPU_CAPABILITIES_SHIM_H
#include <stdint.h>

extern uint64_t platform_test_capabilities;
#define _COMM_PAGE_CPU_CAPABILITIES64 ((uintptr_t)&platform_test_capabilities)

#define kHasAVX2_0 0x20000000
#define kHasENFSTRG 0x08000000

#endif
