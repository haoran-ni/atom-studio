# src/ui/CMakeLists.txt
# UI library with QML components and backend-specific viewports

# ---------------------------------------------------------------------------
# Shared UI sources (always built)
# ---------------------------------------------------------------------------
set(UI_SHARED_SOURCES
    components/FileController.cpp
    components/FileController.h
    components/StructureModel.cpp
    components/StructureModel.h
)

# ---------------------------------------------------------------------------
# OpenGL viewport (built when OpenGL backend is available)
# ---------------------------------------------------------------------------
set(UI_OPENGL_SOURCES
    components/OpenGLViewport.cpp
    components/OpenGLViewport.h
)

# ---------------------------------------------------------------------------
# Vulkan viewport (future)
# ---------------------------------------------------------------------------
set(UI_VULKAN_SOURCES
    components/VulkanViewport.cpp
    components/VulkanViewport.h
)

# ---------------------------------------------------------------------------
# Metal viewport (macOS)
# ---------------------------------------------------------------------------
set(UI_METAL_SOURCES
    components/MetalViewport.h
    components/MetalViewport.mm
)

# ---------------------------------------------------------------------------
# Select viewport implementations based on available backends
# ---------------------------------------------------------------------------
set(UI_BACKEND_SOURCES)

# OpenGL viewport — enabled on all platforms for now
list(APPEND UI_BACKEND_SOURCES ${UI_OPENGL_SOURCES})

# Vulkan viewport — placeholder, always included for now (empty impl)
list(APPEND UI_BACKEND_SOURCES ${UI_VULKAN_SOURCES})

# Metal viewport — macOS
if(APPLE)
    list(APPEND UI_BACKEND_SOURCES ${UI_METAL_SOURCES})
endif()

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
add_library(atom-ui STATIC
    ${UI_SHARED_SOURCES}
    ${UI_BACKEND_SOURCES}
)

target_include_directories(atom-ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(atom-ui PUBLIC
    atom-data
    atom-io
    atom-render
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::OpenGL
    Qt6::Widgets
)

# Optional Vulkan linking for future use
if(Vulkan_FOUND)
    target_link_libraries(atom-ui PUBLIC Vulkan::Vulkan)
    target_compile_definitions(atom-ui PUBLIC ATOM_HAS_VULKAN)
endif()

# Metal viewport support on macOS
if(APPLE)
    target_link_libraries(atom-ui PUBLIC "-framework Metal" "-framework QuartzCore")
    target_compile_definitions(atom-ui PUBLIC ATOM_HAS_METAL)
endif()
