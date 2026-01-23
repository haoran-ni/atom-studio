# src/data/CMakeLists.txt
# Data model library - atomic structure, bonds, unit cell

add_library(atom-data STATIC
    ElementData.cpp
    ElementData.h
    AtomicStructure.cpp
    AtomicStructure.h
    BondList.cpp
    BondList.h
    UnitCell.cpp
    UnitCell.h
)

target_include_directories(atom-data PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# No external dependencies for data layer
# Uses only standard C++ library
