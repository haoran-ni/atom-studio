# src/render/CMakeLists.txt
# Rendering library - OpenGL renderer, camera, shaders

add_library(atom-render STATIC
    Camera.cpp
    Camera.h
    Renderer.h
    RenderSettings.h
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

target_include_directories(atom-render PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(atom-render PUBLIC
    atom-data
    Qt6::Core
    Qt6::Gui
    Qt6::OpenGL
)
