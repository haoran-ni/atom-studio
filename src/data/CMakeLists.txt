# src/data/CMakeLists.txt
# Data model library - atomic structure, bonds, lattice

add_library(atom-data STATIC
    ElementData.cpp
    ElementData.h
    BondList.cpp
    BondList.h
    NeighborList.cpp
    NeighborList.h
    Structure.cpp
    Structure.h
    StructureOperations.cpp
    StructureOperations.h
)

target_include_directories(atom-data PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# No external dependencies for data layer
# Uses only standard C++ library

if(BUILD_TESTING)
    add_executable(atom-data-neighborlist-test
        tests/NeighborListTest.cpp
    )
    target_link_libraries(atom-data-neighborlist-test PRIVATE atom-data)
    add_test(NAME atom-data-neighborlist COMMAND atom-data-neighborlist-test)

    add_executable(atom-data-structureops-test
        tests/StructureOperationsTest.cpp
    )
    target_link_libraries(atom-data-structureops-test PRIVATE atom-data)
    add_test(NAME atom-data-structureops COMMAND atom-data-structureops-test)
endif()
