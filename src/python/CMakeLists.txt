# src/python/CMakeLists.txt
# Python integration library - ASE reader and Python runtime management

add_library(atom-python STATIC
    PythonRuntime.cpp
    PythonRuntime.h
    ASEReader.cpp
    ASEReader.h
)

target_include_directories(atom-python PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
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
