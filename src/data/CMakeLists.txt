# src/data/CMakeLists.txt
# Data model library - atomic structure, bonds, lattice

add_library(atom-data STATIC
    ElementData.cpp
    ElementData.h
    BondList.cpp
    BondList.h
    Structure.cpp
    Structure.h
)

target_include_directories(atom-data PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# No external dependencies for data layer
# Uses only standard C++ library
