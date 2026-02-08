# src/render/CMakeLists.txt
# Rendering library — common abstractions + platform-conditional backends

# ---------------------------------------------------------------------------
# Common sources (always built, backend-agnostic)
# ---------------------------------------------------------------------------
set(RENDER_COMMON_SOURCES
    common/Camera.cpp
    common/Camera.h
    common/Renderer.h
    common/RenderSettings.h
)

# ---------------------------------------------------------------------------
# OpenGL backend (available on all platforms as fallback / current default)
# ---------------------------------------------------------------------------
set(RENDER_OPENGL_SOURCES
    opengl/ShaderManager.cpp
    opengl/ShaderManager.h
    opengl/SphereRenderer.cpp
    opengl/SphereRenderer.h
    opengl/BondRenderer.cpp
    opengl/BondRenderer.h
    opengl/UnitCellRenderer.cpp
    opengl/UnitCellRenderer.h
    opengl/OpenGLRenderer.cpp
    opengl/OpenGLRenderer.h
    opengl/RayTracingRenderer.cpp
    opengl/RayTracingRenderer.h
)

# ---------------------------------------------------------------------------
# Vulkan backend (future — Windows/Linux primary)
# ---------------------------------------------------------------------------
set(RENDER_VULKAN_SOURCES
    # vulkan/VulkanRenderer.cpp
    # vulkan/VulkanRenderer.h
)

# ---------------------------------------------------------------------------
# Metal backend (future — macOS primary)
# ---------------------------------------------------------------------------
set(RENDER_METAL_SOURCES
    # metal/MetalRenderer.cpp
    # metal/MetalRenderer.h
)

# ---------------------------------------------------------------------------
# Select backends based on platform
# ---------------------------------------------------------------------------
set(RENDER_BACKEND_SOURCES)
set(RENDER_BACKEND_LIBS)

# OpenGL — enabled on all platforms for now
list(APPEND RENDER_BACKEND_SOURCES ${RENDER_OPENGL_SOURCES})
list(APPEND RENDER_BACKEND_LIBS Qt6::OpenGL)

# Future: Vulkan backend for Windows/Linux
# if(Vulkan_FOUND AND NOT APPLE)
#     list(APPEND RENDER_BACKEND_SOURCES ${RENDER_VULKAN_SOURCES})
#     list(APPEND RENDER_BACKEND_LIBS Vulkan::Vulkan)
# endif()

# Future: Metal backend for macOS
# if(APPLE)
#     list(APPEND RENDER_BACKEND_SOURCES ${RENDER_METAL_SOURCES})
#     list(APPEND RENDER_BACKEND_LIBS "-framework Metal" "-framework MetalKit")
# endif()

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
add_library(atom-render STATIC
    ${RENDER_COMMON_SOURCES}
    ${RENDER_BACKEND_SOURCES}
)

target_include_directories(atom-render PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
)

# Common dependencies
target_link_libraries(atom-render PUBLIC
    atom-data
    Qt6::Core
    Qt6::Gui
)

# Backend-specific dependencies
target_link_libraries(atom-render PUBLIC
    ${RENDER_BACKEND_LIBS}
)
