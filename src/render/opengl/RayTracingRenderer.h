#pragma once

#include "../common/Renderer.h"
#include "ShaderManager.h"
#include "ViewportAxesRenderer.h"
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <memory>

namespace atom::render {

/**
 * @brief Full-screen fragment-shader ray tracing renderer
 *
 * Traces primary rays, shadow rays, and ambient occlusion rays per pixel
 * using OpenGL 4.1 texture buffer objects for atom data.
 * Progressive accumulation refines the image when the camera is stationary.
 */
class RayTracingRenderer : public Renderer, protected QOpenGLFunctions {
public:
    RayTracingRenderer();
    ~RayTracingRenderer() override;

    // Renderer interface
    bool initialize() override;
    void cleanup() override;
    void resize(int width, int height) override;
    void setStructure(const data::Structure* structure) override;
    void render(const Camera& camera, const RenderSettings& settings) override;
    void invalidateAtomData() override;
    void invalidateBondData() override;

    // Progressive rendering state
    int sampleCount() const { return m_sampleCount; }
    bool isConverged() const { return m_sampleCount >= m_settings.maxRTSamples; }
    void resetAccumulation();

private:
    // Initialization helpers
    bool compileShaders();
    void createFullScreenQuad();
    void createAccumulationFBO();
    void uploadAtomData();

    // Render passes
    void renderRTPass(const Camera& camera);
    void renderDisplayPass(const Camera& camera);

    // --- GL resources ---

    // Shaders
    std::unique_ptr<QOpenGLShaderProgram> m_rtShader;
    std::unique_ptr<QOpenGLShaderProgram> m_displayShader;
    ShaderManager m_overlayShaderManager;
    ViewportAxesRenderer m_viewportAxesRenderer;

    // Full-screen quad
    GLuint m_quadVAO = 0;
    GLuint m_quadVBO = 0;

    // Accumulation FBO (RGBA32F, additive blending)
    GLuint m_accumFBO = 0;
    GLuint m_accumTexture = 0;

    // Atom data via Texture Buffer Objects
    GLuint m_atomPosBuf = 0;      // Buffer: vec4(x, y, z, radius) per atom
    GLuint m_atomPosTex = 0;      // Texture view (RGBA32F)
    GLuint m_atomColorBuf = 0;    // Buffer: vec4(r, g, b, a) per atom
    GLuint m_atomColorTex = 0;    // Texture view (RGBA32F)

    // BVH data via Texture Buffer Objects
    GLuint m_bvhNodeMinBuf = 0;   // Buffer: vec4(min.xyz, maxRadius) per node
    GLuint m_bvhNodeMinTex = 0;   // Texture view (RGBA32F)
    GLuint m_bvhNodeMaxBuf = 0;   // Buffer: vec4(max.xyz, pad) per node
    GLuint m_bvhNodeMaxTex = 0;   // Texture view (RGBA32F)
    GLuint m_bvhNodeMetaBuf = 0;  // Buffer: uvec4(left, right, first, count) per node
    GLuint m_bvhNodeMetaTex = 0;  // Texture view (RGBA32UI)
    GLuint m_bvhPrimBuf = 0;      // Buffer: uint primitive indices
    GLuint m_bvhPrimTex = 0;      // Texture view (R32UI)

    // --- State ---
    RenderSettings m_settings;  // Local copy for isConverged() / state hashing
    const data::Structure* m_structure = nullptr;
    int m_width = 0;
    int m_height = 0;
    int m_atomCount = 0;
    int m_bvhNodeCount = 0;
    int m_sampleCount = 0;
    bool m_initialized = false;
    bool m_atomDataDirty = true;
    uint64_t m_lastStateHash = 0;
};

} // namespace atom::render
