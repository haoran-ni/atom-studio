#pragma once

#include <memory>

namespace atom::render::metal {

/// Compiles MSL shader sources at runtime, caches MTLRenderPipelineState
/// and MTLDepthStencilState objects for each rendering pass.
class MetalShaderLibrary {
public:
    MetalShaderLibrary();
    ~MetalShaderLibrary();

    // Non-copyable
    MetalShaderLibrary(const MetalShaderLibrary&) = delete;
    MetalShaderLibrary& operator=(const MetalShaderLibrary&) = delete;

    /// Compile all shaders and create pipeline states.
    /// @param device  Raw pointer to id<MTLDevice> (cast from void*)
    /// @param rasterSampleCount Sample count for rasterized pipelines
    ///        (sphere/bond/line/display/RT unit-cell overlay).
    bool initialize(void* device, int rasterSampleCount = 1);
    void cleanup();
    bool isInitialized() const { return m_initialized; }

    // Pipeline state accessors (return id<MTLRenderPipelineState> as void*)
    void* spherePipeline() const;
    /// [[depth(greater)]] variant — requires near-tangent billboard placement
    /// (SceneUniforms::sphereEarlyZ = 1); keeps conservative early-Z alive.
    void* spherePipelineEarlyZ() const;
    void* bondPipeline() const;
    /// Variant whose vertex stage reads precomputed per-bond frame data.
    void* bondPipelinePrecomputed() const;
    void* bondOutlinePipeline() const;
    void* bondOutlinePipelinePrecomputed() const;
    /// Compute pipeline filling the per-bond frame buffer (id<MTLComputePipelineState>).
    void* bondFrameComputePipeline() const;
    void* solidCylinderPipeline() const;
    void* viewportAxesPipeline() const;
    void* linePipeline() const;
    void* rtPipeline() const;
    void* displayPipeline() const;
    /// Single-sample display pipeline for overlay-free frames (skips the
    /// MSAA render target + resolve entirely).
    void* displayPipelineSingleSample() const;
    void* rtUnitCellCylinderPipeline() const;
    void* rtUnitCellSpherePipeline() const;

    // Depth stencil state accessors (return id<MTLDepthStencilState> as void*)
    void* depthLessWriteState() const;
    void* depthDisabledState() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_initialized = false;
};

} // namespace atom::render::metal
