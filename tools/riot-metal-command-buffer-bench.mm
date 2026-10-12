#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <vector>
#include <algorithm>
int main() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) return 1;
        printf("DEVICE: %s\n", [[device name] UTF8String]);
        id<MTLCommandQueue> queue = [device newCommandQueue];
        bool submit = getenv("RIOT_BENCH_SUBMIT") != nullptr;
        unsigned iterations = submit ? 2000 : 10000;
        if (const char* setting = getenv("RIOT_BENCH_ITERATIONS")) iterations = strtoul(setting, nullptr, 10);
        unsigned passes = 1;
        if (const char* setting = getenv("RIOT_BENCH_PASSES")) passes = strtoul(setting, nullptr, 10);
        if (!iterations || iterations > 1000000 || !passes || passes > 4096) return 3;
        bool split = getenv("RIOT_BENCH_SPLIT") != nullptr;
        std::vector<double> frameTimes;
        MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:16 height:16 mipmapped:NO];
        descriptor.storageMode = MTLStorageModePrivate;
        descriptor.usage = MTLTextureUsageRenderTarget;
        id<MTLTexture> color = submit ? [device newTextureWithDescriptor:descriptor] : nil;
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = color;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(1,0,0,1);
        // Warm the driver and exercise discarded recording buffers. Each scope
        // ends the buffer lifetime; the exclusive pool can then be leased again.
        for (unsigned i = 0; i < 100; ++i) {
            @autoreleasepool { if (![queue commandBuffer]) return 2; }
        }
        struct rusage before = {}, after = {};
        getrusage(RUSAGE_SELF, &before);
        mach_timebase_info_data_t timebase;
        mach_timebase_info(&timebase);
        uint64_t start = mach_absolute_time();
        for (unsigned i = 0; i < iterations; ++i) {
            uint64_t frameStart = mach_absolute_time();
            @autoreleasepool {
                id<MTLCommandBuffer> commands = [queue commandBuffer];
                if (!commands) return 2;
                if (submit) {
                    for (unsigned p = 0; p < passes; ++p) {
                        if (split && p) commands = [queue commandBuffer];
                        if (!commands) return 2;
                        id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
                        [encoder endEncoding];
                        if (split) [commands commit];
                    }
                    if (!split) [commands commit];
                    [commands waitUntilCompleted];
                }
            }
            frameTimes.push_back((mach_absolute_time() - frameStart) * (double)timebase.numer / timebase.denom / 1e6);
        }
        std::sort(frameTimes.begin(), frameTimes.end());
        printf("FRAMEBENCH: passes=%u split=%u p50=%.3fms p99=%.3fms max=%.3fms\n", passes, split,
            frameTimes[frameTimes.size()/2], frameTimes[(frameTimes.size()-1)*99/100], frameTimes.back());
        uint64_t end = mach_absolute_time();
        getrusage(RUSAGE_SELF, &after);
        printf("BENCH: %u %s command buffers %.3f ms minor_faults=%ld\n",
               iterations, submit ? "clear/submitted" : "discarded", (end-start) * (double)timebase.numer / timebase.denom / 1e6,
               after.ru_minflt-before.ru_minflt);
        [color release];
        [queue release];
        [device release];
    }
    return 0;
}
