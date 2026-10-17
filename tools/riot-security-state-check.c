#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/codesign.h>
#define PRIVATE
#include <sys/csr.h>
extern int csops(pid_t, unsigned int, void*, size_t);
extern int __csrctl(uint32_t, void*, size_t);
extern int __mac_syscall(const char*, int, void*);
int main(void) {
    uint32_t flags=0xdeadbeef, active=0;
    int failures=0;
    failures += csops(0,CS_OPS_STATUS,&flags,sizeof(flags)) != 0 || flags != 0;
    errno=0;
    failures += csops(0,CS_OPS_STATUS,(void*)1,sizeof(flags)) != -1 || errno != EFAULT;
    errno=0;
    failures += csops(0,CS_OPS_MARKHARD,NULL,0) != -1 || errno != ENOTSUP;
    unsigned char hash[20]; errno=0;
    failures += csops(0,CS_OPS_CDHASH,hash,sizeof(hash)) != -1 || errno != EINVAL;
    failures += __csrctl(CSR_SYSCALL_GET_ACTIVE_CONFIG,&active,sizeof(active)) != 0 || active != CSR_VALID_FLAGS;
    uint32_t requested=CSR_ALLOW_UNRESTRICTED_FS;
    failures += __csrctl(CSR_SYSCALL_CHECK,&requested,sizeof(requested)) != 0;
    struct { unsigned char hash[20]; uint64_t result; } query={{0},0xdeadbeef};
    errno=0;
    failures += __mac_syscall("AMFI",95,&query) != -1 || errno != ENOTSUP || query.result != 0;
    printf("SECURITY_STATE status=0x%x csr_allowances=0x%x fabricated_trust=0 failures=%d\n",flags,active,failures);
    return failures ? 1 : 0;
}
