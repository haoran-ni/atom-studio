# src/ui/CMakeLists.txt
# UI library with QML components

add_library(atom-ui STATIC
    components/VulkanViewport.cpp
    components/VulkanViewport.h
)

target_include_directories(atom-ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(atom-ui PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
)

# Optional Vulkan linking for future use
if(Vulkan_FOUND)
    target_link_libraries(atom-ui PUBLIC Vulkan::Vulkan)
    target_compile_definitions(atom-ui PUBLIC ATOM_HAS_VULKAN)
endif()
