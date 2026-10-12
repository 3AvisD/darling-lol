#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <Metal/MTLBufferInternal.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
static void verifyResource(bool value) { if (!value) abort(); }
struct Worker { MTLBufferInternal* buffer; Indium::Buffer* expected; };
static void* readResource(void* argument) {
    auto* worker = static_cast<Worker*>(argument);
    @autoreleasepool {
        for (unsigned i=0;i<100000;++i) {
            auto owner=worker->buffer.buffer;
            verifyResource(owner.get()==worker->expected && static_cast<unsigned char*>(owner->contents())[0]==197);
        }
        [worker->buffer release];
    }
    return nullptr;
}
int main() {
    @autoreleasepool {
        id<MTLDevice> device=MTLCreateSystemDefaultDevice(); verifyResource(device!=nil);
        MTLBufferInternal* buffer=(MTLBufferInternal*)[device newBufferWithLength:16 options:MTLResourceStorageModeShared]; verifyResource(buffer!=nil);
        static_cast<unsigned char*>(buffer.contents)[0]=197;
        auto held=buffer.buffer;
        mach_timebase_info_data_t timebase;mach_timebase_info(&timebase);
        uintptr_t checksum=0;const unsigned count=2000000;
        const uint64_t begin=mach_absolute_time();
        for (unsigned i=0;i<count;++i) { auto owner=buffer.buffer;checksum^=reinterpret_cast<uintptr_t>(owner.get()); }
        const uint64_t end=mach_absolute_time();
        printf("BENCH: %u immutable resource getters %.3f ns/get checksum=%llu\n",count,(end-begin)*(double)timebase.numer/timebase.denom/count,(unsigned long long)checksum);
        Worker workers[4];pthread_t threads[4];
        for (unsigned i=0;i<4;++i) {workers[i]={(MTLBufferInternal*)[buffer retain],held.get()};verifyResource(pthread_create(&threads[i],nullptr,readResource,&workers[i])==0);}
        [buffer release];
        for (auto thread:threads) verifyResource(pthread_join(thread,nullptr)==0);
        verifyResource(static_cast<unsigned char*>(held->contents())[0]==197);held.reset();[device release];
        puts("PASS: 400000 concurrent immutable getters and resource owner outlives Objective-C wrapper");
    }
}
