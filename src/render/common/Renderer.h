#pragma once

#include "RenderSettings.h"

namespace atom::data {
class Structure;
}

namespace atom::render {

class Camera;

/**
 * @brief Abstract renderer interface
 *
 * Base class for different rendering backends (OpenGL, Vulkan, etc.)
 */
class Renderer {
public:
    virtual ~Renderer() = default;

    /**
     * @brief Initialize the renderer
     * @return true if initialization succeeded
     */
    virtual bool initialize() = 0;

    /**
     * @brief Clean up resources
     */
    virtual void cleanup() = 0;

    /**
     * @brief Resize the render target
     * @param width New width in pixels
     * @param height New height in pixels
     */
    virtual void resize(int width, int height) = 0;

    /**
     * @brief Set the atomic structure to render
     * @param structure Pointer to structure (may be null to clear)
     */
    virtual void setStructure(const data::Structure* structure) = 0;

    /**
     * @brief Render a frame
     * @param camera Camera for view/projection matrices
     */
    virtual void render(const Camera& camera) = 0;

    /**
     * @brief Get render settings
     */
    RenderSettings& settings() { return m_settings; }
    const RenderSettings& settings() const { return m_settings; }

    /**
     * @brief Mark that atom data needs to be re-uploaded
     */
    virtual void invalidateAtomData() = 0;

    /**
     * @brief Mark that bond data needs to be re-uploaded
     */
    virtual void invalidateBondData() = 0;

protected:
    RenderSettings m_settings;
};

} // namespace atom::render
