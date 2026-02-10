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
 * Camera and RenderSettings are owned externally (by the viewport) and
 * passed into render() so that all renderers share the same state.
 */
class Renderer {
public:
    virtual ~Renderer() = default;

    virtual bool initialize() = 0;
    virtual void cleanup() = 0;
    virtual void resize(int width, int height) = 0;
    virtual void setStructure(const data::Structure* structure) = 0;

    /**
     * @brief Render a frame
     * @param camera Camera for view/projection matrices
     * @param settings Render settings (lighting, visibility, etc.)
     */
    virtual void render(const Camera& camera, const RenderSettings& settings) = 0;

    virtual void invalidateAtomData() = 0;
    virtual void invalidateBondData() = 0;
};

} // namespace atom::render
