# src/python/CMakeLists.txt
# Python integration library - ASE reader and Python runtime management

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    shell/structure_bridge.py shell/interactive_worker.py)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/shell/structure_bridge.py" ATOM_BRIDGE_SOURCE)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/shell/interactive_worker.py" ATOM_SHELL_SOURCE)
configure_file(PythonSources.h.in PythonSources.h @ONLY)

add_library(atom-python STATIC
    PythonRuntime.cpp
    PythonRuntime.h
    ASEReader.cpp
    ASEReader.h
    ASEStructureBridge.cpp
    ASEStructureBridge.h
)

target_include_directories(atom-python PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Link to data library for Structure type
target_link_libraries(atom-python PUBLIC
    atom-data
)

# Link Python and pybind11
target_link_libraries(atom-python PUBLIC
    Python3::Python
    Python3::NumPy
    pybind11::embed
)

# Enable C++17 filesystem
target_compile_features(atom-python PUBLIC cxx_std_17)

if(BUILD_TESTING)
    add_executable(atom-python-bridge-test tests/ASEStructureBridgeTest.cpp)
    target_link_libraries(atom-python-bridge-test PRIVATE atom-python)
    add_test(NAME atom-python-bridge COMMAND atom-python-bridge-test)
    # This standalone test embeds libpython directly, which does not discover
    # the build venv from its own executable path. Use the selected interpreter's
    # packages without changing the application's isolated bundled runtime.
    set_tests_properties(atom-python-bridge PROPERTIES
        ENVIRONMENT "PYTHONPATH=${Python3_SITELIB};PYTHONNOUSERSITE=1"
        TIMEOUT 30
    )
endif()
