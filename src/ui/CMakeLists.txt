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
    components/StructureDocument.h
    components/ViewportSelection.cpp
    components/ViewportSelection.h
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
    Qt6::Concurrent
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

# Exercise structure export and QML controls without an embedded Python runtime.
if(BUILD_TESTING)
    add_executable(atom-ui-multiple-structures-test tests/MultipleStructuresTest.cpp)
    target_link_libraries(atom-ui-multiple-structures-test PRIVATE atom-ui)
    add_test(NAME atom-ui-multiple-structures COMMAND atom-ui-multiple-structures-test -platform offscreen)
    set_tests_properties(atom-ui-multiple-structures PROPERTIES TIMEOUT 45)
    if(APPLE)
        add_executable(atom-ui-metal-viewport-test tests/MetalViewportTest.mm)
        target_link_libraries(atom-ui-metal-viewport-test PRIVATE atom-ui)
        add_test(NAME atom-ui-metal-viewport COMMAND atom-ui-metal-viewport-test)
        set_tests_properties(atom-ui-metal-viewport PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 90 ENVIRONMENT "MTL_DEBUG_LAYER=1")
    endif()
    add_executable(atom-ui-structure-export-test tests/StructureExportTest.cpp)
    target_link_libraries(atom-ui-structure-export-test PRIVATE atom-ui)
    add_test(NAME atom-structure-export
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/tests/check_structure_exports.py"
            --exporter $<TARGET_FILE:atom-ui-structure-export-test>)
    set_tests_properties(atom-structure-export PROPERTIES TIMEOUT 90)

    find_package(Qt6 QUIET COMPONENTS QuickTest)
    if(TARGET Qt6::QuickTest)
        add_executable(atom-ui-replication-test
            tests/ReplicationTest.cpp
            ${PROJECT_SOURCE_DIR}/resources/resources.qrc
        )
        target_link_libraries(atom-ui-replication-test PRIVATE atom-ui Qt6::QuickTest Qt6::QuickControls2)
        target_compile_definitions(atom-ui-replication-test PRIVATE
            QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/replication"
        )
        add_test(NAME atom-ui-replication COMMAND atom-ui-replication-test -platform offscreen)
        set_tests_properties(atom-ui-replication PROPERTIES
            ENVIRONMENT "QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Fusion"
            TIMEOUT 30
        )

        add_executable(atom-ui-color-picker-test
            tests/ColorPickerTest.cpp
            ${PROJECT_SOURCE_DIR}/resources/resources.qrc
        )
        target_link_libraries(atom-ui-color-picker-test PRIVATE Qt6::QuickTest Qt6::QuickControls2)
        target_compile_definitions(atom-ui-color-picker-test PRIVATE
            QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests"
        )
        add_test(NAME atom-ui-color-picker COMMAND atom-ui-color-picker-test -platform offscreen
            -input "${CMAKE_CURRENT_SOURCE_DIR}/tests/tst_ColorPicker.qml")
        set_tests_properties(atom-ui-color-picker PROPERTIES
            ENVIRONMENT "QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Fusion"
            TIMEOUT 30
        )

        add_executable(atom-ui-image-export-test
            tests/ImageExportTest.cpp
            ${PROJECT_SOURCE_DIR}/resources/resources.qrc
        )
        target_link_libraries(atom-ui-image-export-test PRIVATE Qt6::QuickTest Qt6::QuickControls2)
        target_compile_definitions(atom-ui-image-export-test PRIVATE
            QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/image_export"
        )
        add_test(NAME atom-ui-image-export COMMAND atom-ui-image-export-test -platform offscreen)
        set_tests_properties(atom-ui-image-export PROPERTIES
            ENVIRONMENT "QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Fusion"
            TIMEOUT 30
        )
    endif()
endif()
