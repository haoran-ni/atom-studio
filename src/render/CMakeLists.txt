# src/render/CMakeLists.txt
# Rendering library — common abstractions + platform-conditional backends

# ---------------------------------------------------------------------------
# Common sources (always built, backend-agnostic)
# ---------------------------------------------------------------------------
set(RENDER_COMMON_SOURCES
    common/BondRenderData.cpp
    common/BondRenderData.h
    common/BVH.cpp
    common/BVH.h
    common/Camera.cpp
    common/Camera.h
    common/Picking.cpp
    common/Picking.h
    common/RenderStateHash.cpp
    common/RenderStateHash.h
    common/Renderer.h
    common/RenderSettings.h
)

# ---------------------------------------------------------------------------
# OpenGL backend (available on all platforms as fallback / current default)
# ---------------------------------------------------------------------------
set(RENDER_OPENGL_SOURCES
    opengl/CylinderMesh.cpp
    opengl/CylinderMesh.h
    opengl/ShaderManager.cpp
    opengl/ShaderManager.h
    opengl/SphereRenderer.cpp
    opengl/SphereRenderer.h
    opengl/BondRenderer.cpp
    opengl/BondRenderer.h
    opengl/GizmoRenderer.cpp
    opengl/GizmoRenderer.h
    opengl/UnitCellRenderer.cpp
    opengl/UnitCellRenderer.h
    opengl/ViewportAxesRenderer.cpp
    opengl/ViewportAxesRenderer.h
    opengl/OpenGLRenderer.cpp
    opengl/OpenGLRenderer.h
    opengl/RayTracingRenderer.cpp
    opengl/RayTracingRenderer.h
)

# ---------------------------------------------------------------------------
# Vulkan backend (future — Windows/Linux primary)
# ---------------------------------------------------------------------------
set(RENDER_VULKAN_SOURCES
    # Add Vulkan source files here when implemented
)

# ---------------------------------------------------------------------------
# Metal backend (future — macOS primary)
# ---------------------------------------------------------------------------
set(RENDER_METAL_SOURCES
    metal/MetalAsyncOutput.h
    metal/MetalBufferUtil.h
    metal/MetalTypes.h
    metal/MetalShaderLibrary.h
    metal/MetalShaderLibrary.mm
    metal/MetalSphereRenderer.h
    metal/MetalSphereRenderer.mm
    metal/MetalBondRenderer.h
    metal/MetalBondRenderer.mm
    metal/MetalUnitCellShared.h
    metal/MetalUnitCellShared.cpp
    metal/MetalUnitCellRenderer.h
    metal/MetalUnitCellRenderer.mm
    metal/MetalGizmoRenderer.h
    metal/MetalGizmoRenderer.mm
    metal/MetalViewportAxesRenderer.h
    metal/MetalViewportAxesRenderer.mm
    metal/MetalRenderer.h
    metal/MetalRenderer.mm
    metal/MetalRayTracingRenderer.h
    metal/MetalRayTracingRenderer.mm
)

# ---------------------------------------------------------------------------
# Select backends based on platform
# ---------------------------------------------------------------------------
set(RENDER_BACKEND_SOURCES)
set(RENDER_BACKEND_LIBS)

# OpenGL — available on all platforms (current default)
list(APPEND RENDER_BACKEND_SOURCES ${RENDER_OPENGL_SOURCES})
list(APPEND RENDER_BACKEND_LIBS Qt6::OpenGL)

# Vulkan backend — Windows/Linux primary
if(Vulkan_FOUND AND NOT APPLE)
    list(APPEND RENDER_BACKEND_SOURCES ${RENDER_VULKAN_SOURCES})
    list(APPEND RENDER_BACKEND_LIBS Vulkan::Vulkan)
endif()

# Metal backend — macOS primary
if(APPLE)
    list(APPEND RENDER_BACKEND_SOURCES ${RENDER_METAL_SOURCES})
    list(APPEND RENDER_BACKEND_LIBS "-framework Metal" "-framework QuartzCore")
endif()

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

if(BUILD_TESTING)
    add_executable(atom-render-camera-test
        tests/CameraTest.cpp
    )
    target_link_libraries(atom-render-camera-test PRIVATE atom-render)
    add_test(NAME atom-render-camera COMMAND atom-render-camera-test)

    add_executable(atom-render-state-hash-test tests/RenderStateHashTest.cpp)
    target_link_libraries(atom-render-state-hash-test PRIVATE atom-render)
    add_test(NAME atom-render-state-hash COMMAND atom-render-state-hash-test)
endif()
