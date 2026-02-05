# src/io/CMakeLists.txt
# File I/O library - file readers and async loading

add_library(atom-io STATIC
    FileReader.cpp
    FileReader.h
    FileReaderRegistry.cpp
    FileReaderRegistry.h
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
