cmake_minimum_required(VERSION 3.25)

project(atom-studio
    VERSION 0.1.0
    DESCRIPTION "High performance visualization application for atomic structures"
    LANGUAGES CXX
)

include(CTest)

# C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt specific settings
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6 packages
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Quick
    QuickControls2
    OpenGL
    Widgets
)

# Get Qt installation path for macdeployqt
get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)

# Optional: Find Vulkan (for future use)
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    message(STATUS "Vulkan found: ${Vulkan_INCLUDE_DIRS}")
else()
    message(STATUS "Vulkan not found - will be required for rendering in future steps")
endif()

# Find Python and pybind11 for ASE integration
find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter Development NumPy)
find_package(pybind11 CONFIG REQUIRED)

message(STATUS "Python found: ${Python3_EXECUTABLE}")
message(STATUS "Python version: ${Python3_VERSION}")
message(STATUS "NumPy include: ${Python3_NumPy_INCLUDE_DIRS}")
message(STATUS "pybind11 found: ${pybind11_VERSION}")

set(PYTHON_BUNDLE_REQUIREMENTS_FILE "${CMAKE_SOURCE_DIR}/cmake/python-requirements.txt")
set(PYTHON_PACKAGE_SYNC_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/sync_python_packages.py")

# Platform-specific settings
if(APPLE)
    set(CMAKE_MACOSX_BUNDLE ON)
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.atomstudio.app")
    set(MACOSX_BUNDLE_BUNDLE_NAME "ATOM-STUDIO")
    set(MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})

    # Set RPATH settings for finding frameworks in the app bundle
    set(CMAKE_INSTALL_RPATH "@executable_path/../Frameworks")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

    # Fix for deprecated AGL framework on modern macOS
    # AGL (Apple GL) was deprecated and removed - create a stub framework to satisfy Qt's OpenGL module
    # Build stub as a framework during CMake configure
    enable_language(C)
    enable_language(OBJCXX)
    add_library(AGL SHARED ${CMAKE_SOURCE_DIR}/cmake/agl_stub.c)
    set_target_properties(AGL PROPERTIES
        FRAMEWORK TRUE
        FRAMEWORK_VERSION A
        MACOSX_FRAMEWORK_IDENTIFIER com.apple.agl
        PUBLIC_HEADER ""
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Frameworks
    )
endif()

# Add subdirectories
add_subdirectory(src)

set(APP_BUNDLE_SOURCES)
set(APP_ICON_MACOS "${CMAKE_SOURCE_DIR}/resources/temp_logo.icns")
if(APPLE)
    set_source_files_properties(${APP_ICON_MACOS} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    list(APPEND APP_BUNDLE_SOURCES ${APP_ICON_MACOS})
endif()

# Main executable
add_executable(${PROJECT_NAME}
    src/main.cpp
    resources/resources.qrc
    ${APP_BUNDLE_SOURCES}
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    atom-core
    atom-ui
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::OpenGL
    Qt6::Widgets
)

# Add framework search path for AGL stub on macOS
if(APPLE)
    add_dependencies(${PROJECT_NAME} AGL)
    target_link_options(${PROJECT_NAME} PRIVATE
        -F${CMAKE_BINARY_DIR}/Frameworks
    )
endif()

# Set runtime output directory
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

set(PYTHON_RUNTIME_BUNDLE_ROOT "${CMAKE_BINARY_DIR}/bin")

# macOS bundle settings
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_ICON_FILE "temp_logo.icns"
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/resources/Info.plist.in
    )

    # Post-build: Copy AGL stub framework into app bundle
    set(APP_BUNDLE_PATH "${CMAKE_BINARY_DIR}/bin/${PROJECT_NAME}.app")
    set(APP_FRAMEWORKS_DIR "${APP_BUNDLE_PATH}/Contents/Frameworks")
    set(PYTHON_RUNTIME_BUNDLE_ROOT "${APP_BUNDLE_PATH}")

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMENT "Copying AGL stub framework into app bundle..."
        COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_FRAMEWORKS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_BINARY_DIR}/Frameworks/AGL.framework"
            "${APP_FRAMEWORKS_DIR}/AGL.framework"
    )
endif()

add_custom_target(bundle-python ALL
    COMMENT "Synchronizing bundled Python runtime..."
    COMMAND ${CMAKE_COMMAND}
        -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
        -DAPP_BUNDLE_PATH=${PYTHON_RUNTIME_BUNDLE_ROOT}
        -DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}
        -DPYTHON_REQUIREMENTS_FILE=${PYTHON_BUNDLE_REQUIREMENTS_FILE}
        -DPYTHON_PACKAGE_SYNC_SCRIPT=${PYTHON_PACKAGE_SYNC_SCRIPT}
        -P ${CMAKE_SOURCE_DIR}/cmake/BundlePython.cmake
    DEPENDS
        ${PROJECT_NAME}
        ${PYTHON_BUNDLE_REQUIREMENTS_FILE}
        ${PYTHON_PACKAGE_SYNC_SCRIPT}
    VERBATIM
)

add_custom_target(deploy-python
    COMMENT "Ensuring bundled Python runtime is up to date..."
    DEPENDS bundle-python
)

if(APPLE)
    # Custom target to deploy Qt frameworks into the app bundle
    # Run with: cmake --build build --target deploy
    add_custom_target(deploy
        COMMENT "Deploying Qt frameworks and bundled Python into app bundle..."
        COMMAND "${QT_BIN_DIR}/macdeployqt" "${APP_BUNDLE_PATH}" -qmldir=${CMAKE_SOURCE_DIR}/src/ui/qml
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_BINARY_DIR}/Frameworks/AGL.framework"
            "${APP_FRAMEWORKS_DIR}/AGL.framework"
        DEPENDS ${PROJECT_NAME} bundle-python
    )
endif()
