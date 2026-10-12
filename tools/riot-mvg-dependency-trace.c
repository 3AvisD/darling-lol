/* Optional diagnostics: preserve every API result and log no buffer contents. */
#include <CoreFoundation/CoreFoundation.h>
#include <Security/SecStaticCode.h>
#include <Security/SecCode.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <errno.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
extern int csops(pid_t, unsigned int, void *, size_t);
extern int proc_pidinfo(int, int, uint64_t, void *, int);
extern int proc_pidfdinfo(int, int, int, void *, int);

static int is_mvg(void *caller) {
    Dl_info image;
    return dladdr(caller, &image) && image.dli_fname &&
        strstr(image.dli_fname, "/mvg.framework/mvg") != NULL;
}

static void report(const char *api, long result, int error, long operation, void *caller) {
    int saved = errno;
    Dl_info image;
    if (dladdr(caller, &image) && image.dli_fname && strstr(image.dli_fname, "/mvg.framework/mvg")) {
        char line[256];
        int length = snprintf(line, sizeof(line),
            "MVG_DEP api=%s result=%ld errno=%d operation=%ld caller=0x%lx\n",
            api, result, error, operation, (unsigned long)((uintptr_t)caller - (uintptr_t)image.dli_fbase));
        if (length > 0 && length < sizeof(line)) write(2, line, length);
    }
    errno = saved;
}
#define TRACE(type, name, parameters, arguments, operation, observe) \
    static type trace_##name parameters { \
        type result = name arguments; int saved = errno; \
        static unsigned trace_count; \
        if ((observe) && is_mvg(__builtin_return_address(0)) && __sync_fetch_and_add(&trace_count, 1) < 200) \
            report(#name, (long)result, saved, operation, __builtin_return_address(0)); \
        errno = saved; return result; \
    } \
    __attribute__((used, section("__DATA,__interpose"))) \
    static const struct { const void *replacement; const void *original; } \
    interpose_##name = { (const void *)trace_##name, (const void *)name };

TRACE(OSStatus, SecStaticCodeCreateWithPath, (CFURLRef path, SecCSFlags flags, SecStaticCodeRef *out), (path, flags, out), flags, 1)
TRACE(OSStatus, SecStaticCodeCreateWithPathAndAttributes, (CFURLRef path, SecCSFlags flags, CFDictionaryRef attrs, SecStaticCodeRef *out), (path, flags, attrs, out), flags, 1)
TRACE(OSStatus, SecCodeCopySigningInformation, (SecStaticCodeRef code, SecCSFlags flags, CFDictionaryRef *out), (code, flags, out), flags, 1)
TRACE(OSStatus, SecStaticCodeCheckValidity, (SecStaticCodeRef code, SecCSFlags flags, SecRequirementRef req), (code, flags, req), flags, 1)
TRACE(OSStatus, SecStaticCodeCheckValidityWithErrors, (SecStaticCodeRef code, SecCSFlags flags, SecRequirementRef req, CFErrorRef *err), (code, flags, req, err), flags, 1)
TRACE(int, csops, (pid_t pid, unsigned int op, void *data, size_t size), (pid, op, data, size), op, 1)
TRACE(int, proc_pidinfo, (int pid, int flavor, uint64_t arg, void *data, int size), (pid, flavor, arg, data, size), flavor, result <= 0)
TRACE(int, proc_pidfdinfo, (int pid, int fd, int flavor, void *data, int size), (pid, fd, flavor, data, size), flavor, result <= 0)
TRACE(int, sysctl, (int *name, u_int count, void *old, size_t *size, void *new, size_t length), (name, count, old, size, new, length), count, result < 0)
TRACE(int, sysctlbyname, (const char *name, void *old, size_t *size, void *new, size_t length), (name, old, size, new, length), 0, result < 0)
TRACE(io_service_t, IOServiceGetMatchingService, (mach_port_t port, CFDictionaryRef match), (port, match), 0, result == 0)
TRACE(kern_return_t, mach_vm_page_info, (vm_map_t task, mach_vm_address_t addr, vm_page_info_flavor_t flavor, vm_page_info_t info, mach_msg_type_number_t *count), (task, addr, flavor, info, count), flavor, result != KERN_SUCCESS)
TRACE(kern_return_t, task_info, (task_name_t task, task_flavor_t flavor, task_info_t info, mach_msg_type_number_t *count), (task, flavor, info, count), flavor, result != KERN_SUCCESS)
