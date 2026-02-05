# cmake/BundlePython.cmake
# Script to bundle Python interpreter and packages into the application
#
# This script:
# 1. Copies the Python standard library to the bundle
# 2. Installs required packages (ASE, NumPy)
#
# Usage: Called during 'deploy' target build

# Configuration (passed from main CMakeLists.txt)
# PYTHON_EXECUTABLE - Path to Python interpreter
# APP_BUNDLE_PATH - Path to the app bundle (macOS) or install directory

# Function to bundle Python on macOS
function(bundle_python_macos)
    message(STATUS "Bundling Python for macOS...")

    set(RESOURCES_DIR "${APP_BUNDLE_PATH}/Contents/Resources")
    set(PYTHON_BUNDLE_DIR "${RESOURCES_DIR}/python")

    # Get Python prefix (where Python is installed)
    execute_process(
        COMMAND ${PYTHON_EXECUTABLE} -c "import sys; print(sys.prefix)"
        OUTPUT_VARIABLE PYTHON_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    # Get Python version
    execute_process(
        COMMAND ${PYTHON_EXECUTABLE} -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')"
        OUTPUT_VARIABLE PY_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    message(STATUS "  Python prefix: ${PYTHON_PREFIX}")
    message(STATUS "  Python version: ${PY_VERSION}")

    set(PYTHON_LIB_SRC "${PYTHON_PREFIX}/lib/python${PY_VERSION}")
    set(PYTHON_LIB_DST "${PYTHON_BUNDLE_DIR}/lib/python${PY_VERSION}")

    # Remove old bundle if exists
    if(EXISTS "${PYTHON_BUNDLE_DIR}")
        file(REMOVE_RECURSE "${PYTHON_BUNDLE_DIR}")
    endif()

    # Create directory structure
    file(MAKE_DIRECTORY "${PYTHON_BUNDLE_DIR}/lib")

    message(STATUS "  Copying Python standard library (this may take a moment)...")

    # Copy entire standard library, excluding test directories and large unnecessary items
    file(COPY "${PYTHON_LIB_SRC}/"
         DESTINATION "${PYTHON_LIB_DST}"
         PATTERN "test" EXCLUDE
         PATTERN "tests" EXCLUDE
         PATTERN "test_*" EXCLUDE
         PATTERN "__pycache__" EXCLUDE
         PATTERN "*.pyc" EXCLUDE
         PATTERN "*.pyo" EXCLUDE
         PATTERN "idlelib" EXCLUDE
         PATTERN "tkinter" EXCLUDE
         PATTERN "turtledemo" EXCLUDE
         PATTERN "turtle.py" EXCLUDE
         PATTERN "ensurepip" EXCLUDE
         PATTERN "distutils" EXCLUDE
         PATTERN "lib2to3" EXCLUDE
         PATTERN "pydoc_data" EXCLUDE
         PATTERN "site-packages" EXCLUDE
    )

    # Create empty site-packages directory
    file(MAKE_DIRECTORY "${PYTHON_LIB_DST}/site-packages")

    # Install packages using pip
    message(STATUS "  Installing Python packages (ASE, NumPy)...")
    set(SITE_PACKAGES "${PYTHON_LIB_DST}/site-packages")

    execute_process(
        COMMAND ${PYTHON_EXECUTABLE} -m pip install
            --target "${SITE_PACKAGES}"
            --upgrade
            --no-user
            ase numpy
        RESULT_VARIABLE PIP_RESULT
        OUTPUT_VARIABLE PIP_OUTPUT
        ERROR_VARIABLE PIP_ERROR
    )

    if(NOT PIP_RESULT EQUAL 0)
        message(WARNING "  pip install failed: ${PIP_ERROR}")
        message(STATUS "  Trying with --break-system-packages...")
        execute_process(
            COMMAND ${PYTHON_EXECUTABLE} -m pip install
                --target "${SITE_PACKAGES}"
                --upgrade
                --no-user
                --break-system-packages
                ase numpy
            RESULT_VARIABLE PIP_RESULT2
        )
        if(NOT PIP_RESULT2 EQUAL 0)
            message(FATAL_ERROR "Failed to install Python packages")
        endif()
    endif()

    # Report bundle size
    execute_process(
        COMMAND du -sh "${PYTHON_BUNDLE_DIR}"
        OUTPUT_VARIABLE BUNDLE_SIZE
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    message(STATUS "  Python bundle size: ${BUNDLE_SIZE}")

    message(STATUS "  Python bundling complete!")
endfunction()

# Function to bundle Python on Windows
function(bundle_python_windows)
    message(STATUS "Bundling Python for Windows...")

    set(PYTHON_BUNDLE_DIR "${APP_BUNDLE_PATH}/python")

    # Get Python prefix
    execute_process(
        COMMAND ${PYTHON_EXECUTABLE} -c "import sys; print(sys.prefix)"
        OUTPUT_VARIABLE PYTHON_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    # Remove old bundle if exists
    if(EXISTS "${PYTHON_BUNDLE_DIR}")
        file(REMOVE_RECURSE "${PYTHON_BUNDLE_DIR}")
    endif()

    # Create directory
    file(MAKE_DIRECTORY "${PYTHON_BUNDLE_DIR}")

    # Copy Python DLLs
    file(GLOB PYTHON_DLLS "${PYTHON_PREFIX}/*.dll")
    file(COPY ${PYTHON_DLLS} DESTINATION "${PYTHON_BUNDLE_DIR}")

    # Copy Lib directory (excluding tests)
    file(COPY "${PYTHON_PREFIX}/Lib"
         DESTINATION "${PYTHON_BUNDLE_DIR}"
         PATTERN "test" EXCLUDE
         PATTERN "tests" EXCLUDE
         PATTERN "__pycache__" EXCLUDE
         PATTERN "idlelib" EXCLUDE
         PATTERN "tkinter" EXCLUDE
         PATTERN "turtledemo" EXCLUDE
    )

    # Copy DLLs directory
    if(EXISTS "${PYTHON_PREFIX}/DLLs")
        file(COPY "${PYTHON_PREFIX}/DLLs" DESTINATION "${PYTHON_BUNDLE_DIR}")
    endif()

    # Install packages
    set(SITE_PACKAGES "${PYTHON_BUNDLE_DIR}/Lib/site-packages")
    execute_process(
        COMMAND ${PYTHON_EXECUTABLE} -m pip install
            --target "${SITE_PACKAGES}"
            --upgrade
            ase numpy
    )

    message(STATUS "  Python bundling complete!")
endfunction()

# Function to bundle Python on Linux
function(bundle_python_linux)
    message(STATUS "Bundling Python for Linux...")

    set(PYTHON_BUNDLE_DIR "${APP_BUNDLE_PATH}/python")

    execute_process(
        COMMAND ${PYTHON_EXECUTABLE} -c "import sys; print(sys.prefix)"
        OUTPUT_VARIABLE PYTHON_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    execute_process(
        COMMAND ${PYTHON_EXECUTABLE} -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')"
        OUTPUT_VARIABLE PY_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    # Remove old bundle if exists
    if(EXISTS "${PYTHON_BUNDLE_DIR}")
        file(REMOVE_RECURSE "${PYTHON_BUNDLE_DIR}")
    endif()

    file(MAKE_DIRECTORY "${PYTHON_BUNDLE_DIR}/lib")

    # Copy standard library (excluding tests)
    file(COPY "${PYTHON_PREFIX}/lib/python${PY_VERSION}/"
         DESTINATION "${PYTHON_BUNDLE_DIR}/lib/python${PY_VERSION}"
         PATTERN "test" EXCLUDE
         PATTERN "tests" EXCLUDE
         PATTERN "__pycache__" EXCLUDE
         PATTERN "idlelib" EXCLUDE
         PATTERN "tkinter" EXCLUDE
         PATTERN "site-packages" EXCLUDE
    )

    # Create site-packages
    file(MAKE_DIRECTORY "${PYTHON_BUNDLE_DIR}/lib/python${PY_VERSION}/site-packages")

    # Install packages
    set(SITE_PACKAGES "${PYTHON_BUNDLE_DIR}/lib/python${PY_VERSION}/site-packages")
    execute_process(
        COMMAND ${PYTHON_EXECUTABLE} -m pip install
            --target "${SITE_PACKAGES}"
            --upgrade
            ase numpy
    )

    message(STATUS "  Python bundling complete!")
endfunction()

# Main bundling logic
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    bundle_python_macos()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    bundle_python_windows()
else()
    bundle_python_linux()
endif()
