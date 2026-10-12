#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <pthread.h>
#undef NDEBUG
#include <assert.h>
#include <stdio.h>

struct Worker { id<MTLDevice> device; id<MTLCommandQueue> queue; unsigned index; id<MTLTexture> shared; };
static void *run(void *argument) {
    Worker *worker = (Worker *)argument;
    for (unsigned iteration = 0; iteration < 100; ++iteration) {
        @autoreleasepool {
            MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:16 height:16 mipmapped:NO];
            descriptor.storageMode = MTLStorageModeShared;
            descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            id<MTLTexture> texture = worker->shared ? [worker->shared retain] : [worker->device newTextureWithDescriptor:descriptor];
            unsigned char initial[16 * 16 * 4] = {};
            if (!worker->shared) [texture replaceRegion:MTLRegionMake2D(0,0,16,16) mipmapLevel:0 withBytes:initial bytesPerRow:64];
            id<MTLBuffer> readback = [worker->device newBufferWithLength:sizeof(initial) options:MTLResourceStorageModeShared];
            MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = texture;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            const unsigned char red = (worker->index + 1) * 40;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(red / 255.0,0,1,1);
            id<MTLCommandBuffer> commands = [worker->queue commandBuffer];
            id<MTLRenderCommandEncoder> render = [commands renderCommandEncoderWithDescriptor:pass];
            [render endEncoding];
            id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
            [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(16,16,1) toBuffer:readback destinationOffset:0 destinationBytesPerRow:64 destinationBytesPerImage:sizeof(initial)];
            [blit endEncoding];
            [commands commit];
            [commands waitUntilCompleted];
            const unsigned char *pixels = (const unsigned char *)readback.contents;
            for (unsigned p = 0; p < 256; ++p) {
                assert(pixels[p*4] == red && pixels[p*4+1] == 0 && pixels[p*4+2] == 255 && pixels[p*4+3] == 255);
            }
            [readback release];
            [texture release];
        }
    }
    return NULL;
}
int main(void) {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        assert(device);
        id<MTLCommandQueue> queue = [device newCommandQueue];
        pthread_t threads[4]; Worker workers[4];
        for (unsigned i = 0; i < 4; ++i) {
            workers[i] = {device, queue, i, nil};
            assert(pthread_create(&threads[i], NULL, run, &workers[i]) == 0);
        }
        for (auto thread: threads) assert(pthread_join(thread, NULL) == 0);
        MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:16 height:16 mipmapped:NO];
        descriptor.storageMode = MTLStorageModePrivate;
        descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        id<MTLTexture> shared = [device newTextureWithDescriptor:descriptor];
        for (unsigned i = 0; i < 4; ++i) {
            workers[i].shared = shared;
            assert(pthread_create(&threads[i], NULL, run, &workers[i]) == 0);
        }
        for (auto thread: threads) assert(pthread_join(thread, NULL) == 0);
        // Queue more commands than the optional submission worker's backlog
        // limit, without intervening waits. The final copy must observe the
        // final clear from a separate command buffer, after earlier wrappers
        // have been released by their autorelease pools.
        for (unsigned i = 0; i < 130; ++i) {
            @autoreleasepool {
                MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
                pass.colorAttachments[0].texture = shared;
                pass.colorAttachments[0].loadAction = MTLLoadActionClear;
                pass.colorAttachments[0].storeAction = MTLStoreActionStore;
                pass.colorAttachments[0].clearColor = MTLClearColorMake(i / 255.0,0,1,1);
                id<MTLCommandBuffer> commands = [queue commandBuffer];
                id<MTLRenderCommandEncoder> render = [commands renderCommandEncoderWithDescriptor:pass];
                [render endEncoding];
                [commands commit];
            }
        }
        id<MTLBuffer> output = [device newBufferWithLength:16*16*4 options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> copy = [queue commandBuffer];
        // A Load pass must not reuse the earlier Clear semantics, even when
        // format and render-pass compatibility are identical.
        MTLRenderPassDescriptor *preserve = [MTLRenderPassDescriptor renderPassDescriptor];
        preserve.colorAttachments[0].texture = shared;
        preserve.colorAttachments[0].loadAction = MTLLoadActionLoad;
        preserve.colorAttachments[0].storeAction = MTLStoreActionStore;
        preserve.colorAttachments[0].clearColor = MTLClearColorMake(0,1,0,1);
        id<MTLRenderCommandEncoder> load = [copy renderCommandEncoderWithDescriptor:preserve];
        [load endEncoding];
        id<MTLBlitCommandEncoder> blit = [copy blitCommandEncoder];
        [blit copyFromTexture:shared sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                  sourceSize:MTLSizeMake(16,16,1) toBuffer:output destinationOffset:0
                  destinationBytesPerRow:64 destinationBytesPerImage:16*16*4];
        [blit endEncoding]; [copy commit]; [copy waitUntilCompleted];
        const unsigned char *pixels = (const unsigned char *)output.contents;
        for (unsigned p = 0; p < 256; ++p)
            assert(pixels[p*4] == 129 && pixels[p*4+1] == 0 && pixels[p*4+2] == 255 && pixels[p*4+3] == 255);
        [output release];
        puts("PASS: 131 queued submissions, FIFO texture dependencies and released command wrappers");
        puts("PASS: Load preserves pixels after Clear with a different clear color");
        [shared release];
        [queue release];
        puts("PASS: 4 concurrent workers, 800 submissions with private and shared textures, all pixels correct");
    }
    return 0;
}
