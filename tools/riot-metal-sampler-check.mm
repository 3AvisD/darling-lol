#import <Metal/Metal.h>
#import <Metal/MTLSamplerStateInternal.h>
#include <stdio.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>
#import <Metal/MTLTextureInternal.h>
static void verifySampler(bool value, const char* message) {
    if (!value) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}
extern void checkMetalShader(id<MTLDevice>);
extern void checkMetalBatch(id<MTLDevice>, id<MTLSamplerState>);
static void checkCompressedTransfers(id<MTLDevice> device) {
    const MTLPixelFormat formats[] = {
        MTLPixelFormatBC1_RGBA, MTLPixelFormatBC1_RGBA_sRGB, MTLPixelFormatBC2_RGBA,
        MTLPixelFormatBC2_RGBA_sRGB, MTLPixelFormatBC3_RGBA, MTLPixelFormatBC3_RGBA_sRGB,
        MTLPixelFormatBC4_RUnorm, MTLPixelFormatBC4_RSnorm, MTLPixelFormatBC5_RGUnorm,
        MTLPixelFormatBC5_RGSnorm, MTLPixelFormatBC6H_RGBFloat, MTLPixelFormatBC6H_RGBUfloat,
        MTLPixelFormatBC7_RGBAUnorm, MTLPixelFormatBC7_RGBAUnorm_sRGB
    };
    id<MTLCommandQueue> queue = [device newCommandQueue];
    for (auto format : formats) {
        size_t blockBytes = (format == MTLPixelFormatBC1_RGBA || format == MTLPixelFormatBC1_RGBA_sRGB ||
                            format == MTLPixelFormatBC4_RUnorm || format == MTLPixelFormatBC4_RSnorm) ? 8 : 16;
        // An odd extent, padded block rows, nonzero offsets and a tight final mip.
        size_t rowBytes = blockBytes * 4;
        unsigned char input[256], zeros[512];
        for (unsigned i = 0; i < sizeof(input); ++i) input[i] = (i * 37 + 11) & 255;
        memset(zeros, 0xaa, sizeof(zeros));
        id<MTLBuffer> upload = [device newBufferWithBytes:input length:sizeof(input) options:MTLResourceStorageModeShared];
        id<MTLBuffer> output = [device newBufferWithBytes:zeros length:sizeof(zeros) options:MTLResourceStorageModeShared];
        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:9 height:7 mipmapped:YES];
        desc.storageMode = MTLStorageModePrivate; desc.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
        verifySampler(texture != nil, "compressed texture allocation");
        id<MTLCommandBuffer> commands = [queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
        [blit copyFromBuffer:upload sourceOffset:64 sourceBytesPerRow:rowBytes sourceBytesPerImage:0 sourceSize:MTLSizeMake(9,7,1) toTexture:texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
        [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(9,7,1) toBuffer:output destinationOffset:128 destinationBytesPerRow:rowBytes destinationBytesPerImage:0 options:MTLBlitOptionNone];
        [blit copyFromBuffer:upload sourceOffset:0 sourceBytesPerRow:blockBytes sourceBytesPerImage:0 sourceSize:MTLSizeMake(2,1,1) toTexture:texture destinationSlice:0 destinationLevel:2 destinationOrigin:MTLOriginMake(0,0,0)];
        [blit copyFromTexture:texture sourceSlice:0 sourceLevel:2 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(2,1,1) toBuffer:output destinationOffset:0 destinationBytesPerRow:blockBytes destinationBytesPerImage:0 options:MTLBlitOptionNone];
        [blit endEncoding]; [commands commit]; [commands waitUntilCompleted];
        auto result = (unsigned char*)output.contents;
        for (size_t row = 0; row < 2; ++row) {
            verifySampler(memcmp(input + 64 + row * rowBytes, result + 128 + row * rowBytes, blockBytes * 3) == 0, "compressed padded upload/readback");
            verifySampler(result[128 + row * rowBytes + blockBytes * 3] == 0xaa, "compressed row padding preserved");
        }
        verifySampler(memcmp(input, result, blockBytes) == 0 && result[blockBytes] == 0xaa, "compressed mip smaller than a block");
        [texture release];
        desc.storageMode = MTLStorageModeShared;
        texture = [device newTextureWithDescriptor:desc];
        [texture replaceRegion:MTLRegionMake2D(0,0,9,7) mipmapLevel:0 withBytes:input+64 bytesPerRow:rowBytes];
        commands = [queue commandBuffer]; blit = [commands blitCommandEncoder];
        [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(9,7,1) toBuffer:output destinationOffset:128 destinationBytesPerRow:rowBytes destinationBytesPerImage:0 options:MTLBlitOptionNone];
        [blit endEncoding]; [commands commit]; [commands waitUntilCompleted];
        for (size_t row = 0; row < 2; ++row)
            verifySampler(memcmp(input + 64 + row * rowBytes, result + 128 + row * rowBytes, blockBytes * 3) == 0, "compressed replaceRegion upload");
        [texture release]; [upload release]; [output release];
    }
    [queue release];
    fprintf(stderr, "PASS: all 14 BC formats, padded rows, buffer offsets, small mips and replaceRegion\n");
}
int main() {
    NSAutoreleasePool* pool = [NSAutoreleasePool new];
    MTLSamplerDescriptor* desc = [MTLSamplerDescriptor new];
    verifySampler(desc.maxAnisotropy == 1 && desc.normalizedCoordinates && desc.lodMaxClamp == FLT_MAX, "defaults");
    desc.minFilter = MTLSamplerMinMagFilterLinear;
    desc.magFilter = MTLSamplerMinMagFilterLinear;
    desc.mipFilter = MTLSamplerMipFilterLinear;
    desc.sAddressMode = MTLSamplerAddressModeRepeat;
    desc.tAddressMode = MTLSamplerAddressModeMirrorRepeat;
    desc.lodMinClamp = 0.5f;
    desc.lodMaxClamp = 12.5f;
    desc.label = @"League sampler";
    MTLSamplerDescriptor* copy = [desc copy];
    desc.lodMinClamp = 1.5f;
    verifySampler(copy.lodMinClamp == 0.5f && copy.lodMaxClamp == 12.5f && copy.sAddressMode == MTLSamplerAddressModeRepeat, "copy preserves typed properties");
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    verifySampler(device != nil, "Metal device");
    id<MTLSamplerState> state = [device newSamplerStateWithDescriptor:copy];
    verifySampler(state != nil && state.device == device && [state.label isEqualToString:@"League sampler"], "sampler device and label");
    verifySampler(((MTLSamplerStateInternal*)state).sampler != nullptr, "real Indium sampler");
    copy.label = @"changed";
    [copy release]; [desc release];
    verifySampler([state.label isEqualToString:@"League sampler"], "state survives descriptor release");
    MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:16 height:8 mipmapped:YES];
    verifySampler(td.mipmapLevelCount == 5 && td.depth == 1 && td.arrayLength == 1, "texture helper mip levels");
    td.storageMode = MTLStorageModePrivate;
    td.cpuCacheMode = MTLCPUCacheModeWriteCombined;
    verifySampler(td.storageMode == MTLStorageModePrivate && td.cpuCacheMode == MTLCPUCacheModeWriteCombined, "resource options fields preserve each other");
    td.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
    MTLTextureDescriptor* tc = [td copy];
    td.width = 32;
    verifySampler(tc.width == 16 && tc.mipmapLevelCount == 5, "texture descriptor copy");
    id<MTLTexture> texture = [device newTextureWithDescriptor:tc];
    verifySampler(texture != nil && texture.width == 16 && texture.height == 8 && texture.mipmapLevelCount == 5, "GPU texture allocation");
    verifySampler(texture.device == device && texture.storageMode == MTLStorageModePrivate, "GPU texture resource options");
    verifySampler(((MTLTextureInternal*)texture).texture != nullptr, "real Indium texture");
    id<MTLBuffer> upload = [device newBufferWithLength:1024 options:MTLResourceStorageModeShared];
    id<MTLBuffer> readback = [device newBufferWithLength:1536 options:MTLResourceStorageModeShared];
    verifySampler(upload != nil && readback != nil, "transfer buffers");
    unsigned char* input = (unsigned char*)upload.contents;
    unsigned char* output = (unsigned char*)readback.contents;
    memset(input, 0xcc, 1024); memset(output, 0xaa, 1536);
    for (unsigned i = 0; i < 512; ++i) input[256+i] = (i * 37 + 11) & 255;
    id<MTLCommandQueue> queue = [device newCommandQueue];
    id<MTLCommandBuffer> commands = [queue commandBuffer];
    verifySampler(commands.commandQueue == queue, "command buffer retains its queue");
    id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
    verifySampler(blit != nil && blit.device == device, "real blit encoder");
    blit.label = @"texture round trip";
    [blit copyFromBuffer:upload sourceOffset:256 sourceBytesPerRow:64 sourceBytesPerImage:512 sourceSize:MTLSizeMake(16,8,1) toTexture:texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
    [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(16,8,1) toBuffer:readback destinationOffset:512 destinationBytesPerRow:64 destinationBytesPerImage:512 options:MTLBlitOptionNone];
    [blit endEncoding]; [commands commit]; [commands waitUntilCompleted];
    verifySampler(memcmp(input+256, output+512, 512) == 0, "texture upload/readback respects buffer offsets");
    verifySampler(output[0] == 0xaa && output[511] == 0xaa && output[1024] == 0xaa, "readback preserves bytes outside destination");
    commands = [queue commandBuffer]; blit = [commands blitCommandEncoder];
    [blit fillBuffer:readback range:NSMakeRange(0,64) value:0x5a];
    [blit copyFromBuffer:upload sourceOffset:256 toBuffer:readback destinationOffset:128 size:64];
    [blit endEncoding]; [commands commit]; [commands waitUntilCompleted];
    verifySampler(output[0] == 0x5a && output[63] == 0x5a && output[64] == 0xaa, "fill range");
    verifySampler(memcmp(input+256, output+128, 64) == 0, "buffer copy offsets");
    [queue release]; [upload release]; [readback release];
    [tc release]; [texture release];
    NSAutoreleasePool* transferPool = [NSAutoreleasePool new];
    checkCompressedTransfers(device);
    [transferPool drain];
    fprintf(stderr, "PASS: compressed transfer cleanup\n");
    NSAutoreleasePool* batchPool = [NSAutoreleasePool new];
    checkMetalBatch(device, state);
    [batchPool drain];
    fprintf(stderr, "PASS: Metal batch cleanup\n");
    NSAutoreleasePool* shaderPool = [NSAutoreleasePool new];
    checkMetalShader(device);
    [shaderPool drain];
    fprintf(stderr, "PASS: shader cleanup\n");
    [state release]; [device release];
    fprintf(stderr, "PASS: Metal sampler, texture, blit upload/readback and buffer operations\n");
    [pool drain];
    return 0;
}
