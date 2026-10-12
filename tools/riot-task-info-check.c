#include <mach/mach.h>
#include <stdio.h>
int main(void) {
    unsigned completed=0;
    for (unsigned i=0;i<100;i++) {
        integer_t data[256]={0};mach_msg_type_number_t count=256;
        kern_return_t kr=task_info(mach_task_self(),TASK_POWER_INFO,data,&count);
        if(kr!=KERN_INVALID_ARGUMENT)return 1;
        count=256;
        kr=task_info(mach_task_self(),0x7fffffff,data,&count);
        if(kr!=KERN_INVALID_ARGUMENT)return 2;
        task_basic_info_64_data_t basic;count=TASK_BASIC_INFO_64_COUNT;
        if(task_info(mach_task_self(),TASK_BASIC_INFO_64,(task_info_t)&basic,&count)!=KERN_SUCCESS)return 3;
        completed++;
    }
    printf("TASK_INFO unsupported_error=1 supported_ok=1 completed=%u pass=1\n",completed);
    return 0;
}
