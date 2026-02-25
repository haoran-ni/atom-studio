#import <Metal/Metal.h>
#include "MetalGizmoRenderer.h"
#include "MetalShaderLibrary.h"
#include "MetalTypes.h"

namespace atom::render::metal {

struct MetalGizmoRenderer::Impl {
    id<MTLDevice> device = nil;
};

MetalGizmoRenderer::MetalGizmoRenderer()
    : m_impl(std::make_unique<Impl>())
{
}

MetalGizmoRenderer::~MetalGizmoRenderer() {
    cleanup();
}

bool MetalGizmoRenderer::initialize(void* device, MetalShaderLibrary* shaderLibrary) {
    m_impl->device = (__bridge id<MTLDevice>)device;
    m_shaderLibrary = shaderLibrary;
    m_initialized = true;
    return true;
}

void MetalGizmoRenderer::cleanup() {
    m_impl->device = nil;
    m_initialized = false;
}

void MetalGizmoRenderer::render(void* encoderPtr, const SceneUniforms& uniforms,
                                float cx, float cy, float cz, float axisLength,
                                bool depthTest)
{
    if (!m_initialized) return;

    id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)encoderPtr;

    // 6 line segments: ±X (red), ±Y (green), ±Z (blue) — 12 vertices total
    const simd_float4 colorX = { 1.0f, 0.25f, 0.25f, 1.0f };
    const simd_float4 colorY = { 0.25f, 1.0f, 0.25f, 1.0f };
    const simd_float4 colorZ = { 0.25f, 0.50f, 1.0f, 1.0f };
    const float len = axisLength;

    const LineVertex vertices[12] = {
        { { cx,       cy, cz }, 0.0f, colorX },  // +X start
        { { cx + len, cy, cz }, 0.0f, colorX },  // +X end
        { { cx,       cy, cz }, 0.0f, colorX },  // -X start
        { { cx - len, cy, cz }, 0.0f, colorX },  // -X end
        { { cx, cy,       cz }, 0.0f, colorY },  // +Y start
        { { cx, cy + len, cz }, 0.0f, colorY },  // +Y end
        { { cx, cy,       cz }, 0.0f, colorY },  // -Y start
        { { cx, cy - len, cz }, 0.0f, colorY },  // -Y end
        { { cx, cy, cz       }, 0.0f, colorZ },  // +Z start
        { { cx, cy, cz + len }, 0.0f, colorZ },  // +Z end
        { { cx, cy, cz       }, 0.0f, colorZ },  // -Z start
        { { cx, cy, cz - len }, 0.0f, colorZ },  // -Z end
    };

    id<MTLRenderPipelineState> pipeline =
        (__bridge id<MTLRenderPipelineState>)m_shaderLibrary->linePipeline();
    id<MTLDepthStencilState> depthState = depthTest
        ? (__bridge id<MTLDepthStencilState>)m_shaderLibrary->depthLessWriteState()
        : (__bridge id<MTLDepthStencilState>)m_shaderLibrary->depthDisabledState();

    [encoder setRenderPipelineState:pipeline];
    [encoder setDepthStencilState:depthState];
    [encoder setCullMode:MTLCullModeNone];
    [encoder setVertexBytes:&uniforms   length:sizeof(SceneUniforms) atIndex:0];
    [encoder setFragmentBytes:&uniforms length:sizeof(SceneUniforms) atIndex:0];
    [encoder setVertexBytes:vertices    length:sizeof(vertices)      atIndex:1];
    [encoder drawPrimitives:MTLPrimitiveTypeLine vertexStart:0 vertexCount:12];
}

} // namespace atom::render::metal
