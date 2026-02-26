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
    void* bondPipeline() const;
    void* viewportAxesPipeline() const;
    void* linePipeline() const;
    void* rtPipeline() const;
    void* displayPipeline() const;
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
