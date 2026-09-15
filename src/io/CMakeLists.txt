# src/io/CMakeLists.txt
# File I/O library - ASE loading and native geometry export

find_package(Qt6 REQUIRED COMPONENTS Concurrent)

add_library(atom-io STATIC
    AsyncFileLoader.cpp
    AsyncFileLoader.h
    StructureFileWriter.cpp
    StructureFileWriter.h
    AsyncStructureExporter.cpp
    AsyncStructureExporter.h
    StructureSnapshot.cpp
    StructureSnapshot.h
)

target_include_directories(atom-io PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(atom-io PUBLIC
    atom-data
    atom-python
    Qt6::Core
    Qt6::Concurrent
)
