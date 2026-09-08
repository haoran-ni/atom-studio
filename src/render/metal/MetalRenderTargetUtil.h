#pragma once
#import <Metal/Metal.h>

namespace atom::render::metal {

// Only transient attachments may use tile memory. Resolved outputs and RT
// accumulation are persistent, and raster MSAA color survives the overlay pass.
inline id<MTLTexture> makeRenderTarget(id<MTLDevice> device, int width, int height,
                                      MTLPixelFormat format, NSUInteger samples,
                                      bool shaderRead, bool transient = false) {
    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
        width:width height:height mipmapped:NO];
    if (samples > 1) {
        desc.textureType = MTLTextureType2DMultisample;
        desc.sampleCount = samples;
    }
    desc.usage = MTLTextureUsageRenderTarget | (shaderRead ? MTLTextureUsageShaderRead : 0);
    desc.storageMode = transient && [device supportsFamily:MTLGPUFamilyApple2]
        ? MTLStorageModeMemoryless : MTLStorageModePrivate;
    return [device newTextureWithDescriptor:desc];
}

} // namespace atom::render::metal
