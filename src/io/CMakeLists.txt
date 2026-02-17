# src/io/CMakeLists.txt
# File I/O library - async loading around ASE parser

add_library(atom-io STATIC
    AsyncFileLoader.cpp
    AsyncFileLoader.h
)

target_include_directories(atom-io PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(atom-io PUBLIC
    atom-data
    atom-python
    Qt6::Core
)
