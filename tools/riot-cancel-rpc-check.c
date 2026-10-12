#include <errno.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <pthread.h>
extern int __pthread_canceled(int);
static void *check(void *unused) {
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    unsigned long long start = mach_absolute_time();
    for (unsigned i = 0; i < 100000; ++i) {
        for (int action = 0; action < 3; ++action) {
            errno = 0;
            if (__pthread_canceled(action) != -1 || errno != ENOSYS) {
                fprintf(stderr, "Unexpected cancellation response for action %d: errno=%d\n", action, errno);
                return (void *)1;
            }
        }
    }
    unsigned long long ns = (mach_absolute_time() - start) * tb.numer / tb.denom;
    printf("PASS cancellation unsupported responses: 300000 calls, %llu ns/call\n", ns / 300000);
    return NULL;
}
int main(void) {
    pthread_t worker;
    void *result;
    if (pthread_create(&worker, NULL, check, NULL) || pthread_join(worker, &result)) return 1;
    return result != NULL;
}
