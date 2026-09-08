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
set(PYTHON_NATIVE_BUNDLE_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/bundle_macos_python.py")

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

    enable_language(OBJCXX)
    # Metal resources use strong Objective-C members in C++ RAII owners. Without
    # ARC, assigning nil or destroying those owners leaks every retained resource.
    add_compile_options($<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
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

    set(APP_BUNDLE_PATH "${CMAKE_BINARY_DIR}/bin/${PROJECT_NAME}.app")
    set(PYTHON_RUNTIME_BUNDLE_ROOT "${APP_BUNDLE_PATH}")
    # A Qt-free build utility validates the same embedded runtime as the app.
    add_executable(atom-python-runtime-check src/python/tests/BundledRuntimeCheck.cpp)
    target_link_libraries(atom-python-runtime-check PRIVATE atom-python)
    set_target_properties(atom-python-runtime-check PROPERTIES MACOSX_BUNDLE FALSE)
    set(PYTHON_NATIVE_BUNDLE_ARGS
        -DPYTHON_LIBRARY=$<TARGET_FILE:Python3::Python>
        -DPYTHON_NATIVE_BUNDLE_SCRIPT=${PYTHON_NATIVE_BUNDLE_SCRIPT}
        -DAPP_EXECUTABLE=$<TARGET_FILE:${PROJECT_NAME}>
        -DPYTHON_CHECK_EXECUTABLE=$<TARGET_FILE:atom-python-runtime-check>
    )
    set(PYTHON_NATIVE_BUNDLE_DEPENDS atom-python-runtime-check ${PYTHON_NATIVE_BUNDLE_SCRIPT})
    if(BUILD_TESTING)
        add_test(NAME atom-python-bundle
            COMMAND "${Python3_EXECUTABLE}" -I
                "${CMAKE_SOURCE_DIR}/scripts/tests/check_macos_python_bundle.py"
                --app "${APP_BUNDLE_PATH}"
                --check-executable $<TARGET_FILE:atom-python-runtime-check>
                --python-library $<TARGET_FILE:Python3::Python>
        )
        set_tests_properties(atom-python-bundle PROPERTIES TIMEOUT 120)
    endif()
endif()

add_custom_target(bundle-python ALL
    COMMENT "Synchronizing bundled Python runtime..."
    COMMAND ${CMAKE_COMMAND}
        -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
        -DAPP_BUNDLE_PATH=${PYTHON_RUNTIME_BUNDLE_ROOT}
        -DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}
        -DPYTHON_REQUIREMENTS_FILE=${PYTHON_BUNDLE_REQUIREMENTS_FILE}
        -DPYTHON_PACKAGE_SYNC_SCRIPT=${PYTHON_PACKAGE_SYNC_SCRIPT}
        ${PYTHON_NATIVE_BUNDLE_ARGS}
        -P ${CMAKE_SOURCE_DIR}/cmake/BundlePython.cmake
    DEPENDS
        ${PROJECT_NAME}
        ${PYTHON_BUNDLE_REQUIREMENTS_FILE}
        ${PYTHON_PACKAGE_SYNC_SCRIPT}
        ${PYTHON_NATIVE_BUNDLE_DEPENDS}
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
        DEPENDS ${PROJECT_NAME} bundle-python
    )
endif()
