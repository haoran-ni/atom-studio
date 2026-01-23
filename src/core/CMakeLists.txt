# src/core/CMakeLists.txt
# Core application library

add_library(atom-core STATIC
    Application.cpp
    Application.h
)

target_include_directories(atom-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(atom-core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
)
