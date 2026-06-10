#pragma once

#import <Metal/Metal.h>
#include <cstring>

namespace atom::render::metal {

// Buffer-reuse helpers for shared-storage Metal buffers.
//
// Safety contract: callers must only write while no command buffer that
// references the buffer is in flight. Both Metal renderers guarantee this by
// gating all upload work behind their single-frame-in-flight check.

/// Returns a shared-storage buffer with capacity >= length, reusing the
/// existing buffer when it is large enough.
inline id<MTLBuffer> ensureSharedBuffer(id<MTLDevice> device,
                                        id<MTLBuffer> buffer,
                                        size_t length) {
    if (buffer && buffer.length >= length) {
        return buffer;
    }
    return [device newBufferWithLength:length options:MTLResourceStorageModeShared];
}

/// Copies bytes into a (possibly reused) shared-storage buffer.
inline id<MTLBuffer> fillSharedBuffer(id<MTLDevice> device,
                                      id<MTLBuffer> buffer,
                                      const void* bytes,
                                      size_t length) {
    if (length == 0) {
        return buffer;
    }
    buffer = ensureSharedBuffer(device, buffer, length);
    if (buffer) {
        std::memcpy(buffer.contents, bytes, length);
    }
    return buffer;
}

} // namespace atom::render::metal
