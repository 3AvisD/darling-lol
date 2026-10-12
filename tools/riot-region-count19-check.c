/* What does darlingserver return for mach_vm_region_recurse with the long info count (19)? */
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
typedef kern_return_t (*region_fn)(vm_map_t, mach_vm_address_t*, mach_vm_size_t*, natural_t*, vm_region_recurse_info_t, mach_msg_type_number_t*);
int main(void) {
    region_fn server = (region_fn) dlsym(RTLD_DEFAULT, "__darling_rpc_mach_vm_region_recurse");
    if (!server) return 1;
    static int probe;
    mach_vm_address_t starts[] = {0, 0x100000000ull, (mach_vm_address_t)&probe, (mach_vm_address_t)&starts, 0x7fff00000000ull, 0xffffffffff000000ull};
    for (unsigned n = 0; n < sizeof(starts)/sizeof(starts[0]); ++n) for (int cnt = 19; cnt >= 12; cnt -= 7) {
        int info[24]; memset(info, 0x5a, sizeof(info));
        mach_vm_address_t a = starts[n]; mach_vm_size_t s = 0x1111; natural_t d = 77; mach_msg_type_number_t c = cnt;
        kern_return_t kr = server(mach_task_self(), &a, &s, &d, (vm_region_recurse_info_t)info, &c);
        printf("Q %llx cnt=%d -> kr=%d addr=%llx size=%llx depth=%u cnt=%u info:", starts[n], cnt, kr, a, s, d, c);
        for (int i = 0; i < 20; ++i) printf(" %x", info[i]);
        printf("\n");
    }
    return 0;
}
