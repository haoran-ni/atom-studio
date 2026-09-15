# cmake/BundlePython.cmake
# Synchronizes a bundled Python runtime next to the application.
#
# Required variables:
# - PYTHON_EXECUTABLE
# - APP_BUNDLE_PATH
# - PYTHON_REQUIREMENTS_FILE
# - PYTHON_PACKAGE_SYNC_SCRIPT

cmake_minimum_required(VERSION 3.25)
set(PYTHON_STDLIB_FORMAT_VERSION 2)

foreach(required_var
        PYTHON_EXECUTABLE
        APP_BUNDLE_PATH
        PYTHON_REQUIREMENTS_FILE
        PYTHON_PACKAGE_SYNC_SCRIPT)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "BundlePython.cmake requires ${required_var}")
    endif()
endforeach()

function(query_python output_var code)
    execute_process(
        COMMAND "${PYTHON_EXECUTABLE}" -I -c "${code}"
        RESULT_VARIABLE query_result
        OUTPUT_VARIABLE query_output
        ERROR_VARIABLE query_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(NOT query_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to query Python with `${code}`:\n${query_error}")
    endif()

    set(${output_var} "${query_output}" PARENT_SCOPE)
endfunction()

function(write_stdlib_manifest manifest_file)
    file(WRITE "${manifest_file}" [=[
set(ATOM_STUDIO_BUNDLED_PYTHON_EXECUTABLE "@PYTHON_EXECUTABLE@")
set(ATOM_STUDIO_BUNDLED_PYTHON_PREFIX "@PYTHON_PREFIX@")
set(ATOM_STUDIO_BUNDLED_PYTHON_VERSION "@PYTHON_VERSION@")
set(ATOM_STUDIO_BUNDLED_PYTHON_FULL_VERSION "@PYTHON_FULL_VERSION@")
set(ATOM_STUDIO_BUNDLED_PYTHON_ARCHITECTURE "@PYTHON_ARCHITECTURE@")
set(ATOM_STUDIO_BUNDLED_PYTHON_STDLIB_SRC "@PYTHON_STDLIB_SRC@")
set(ATOM_STUDIO_BUNDLED_PYTHON_EXT_SUFFIX "@PYTHON_EXT_SUFFIX@")
set(ATOM_STUDIO_BUNDLED_PYTHON_PLATFORM "@CMAKE_SYSTEM_NAME@")
set(ATOM_STUDIO_BUNDLED_STDLIB_FORMAT "@PYTHON_STDLIB_FORMAT_VERSION@")
]=])

    file(READ "${manifest_file}" manifest_contents)
    string(REPLACE "@PYTHON_EXECUTABLE@" "${PYTHON_EXECUTABLE}" manifest_contents "${manifest_contents}")
    string(REPLACE "@PYTHON_PREFIX@" "${PYTHON_PREFIX}" manifest_contents "${manifest_contents}")
    string(REPLACE "@PYTHON_VERSION@" "${PYTHON_VERSION}" manifest_contents "${manifest_contents}")
    string(REPLACE "@PYTHON_FULL_VERSION@" "${PYTHON_FULL_VERSION}" manifest_contents "${manifest_contents}")
    string(REPLACE "@PYTHON_ARCHITECTURE@" "${PYTHON_ARCHITECTURE}" manifest_contents "${manifest_contents}")
    string(REPLACE "@PYTHON_STDLIB_SRC@" "${PYTHON_STDLIB_SRC}" manifest_contents "${manifest_contents}")
    string(REPLACE "@PYTHON_EXT_SUFFIX@" "${PYTHON_EXT_SUFFIX}" manifest_contents "${manifest_contents}")
    string(REPLACE "@CMAKE_SYSTEM_NAME@" "${CMAKE_SYSTEM_NAME}" manifest_contents "${manifest_contents}")
    string(REPLACE "@PYTHON_STDLIB_FORMAT_VERSION@" "${PYTHON_STDLIB_FORMAT_VERSION}" manifest_contents "${manifest_contents}")
    file(WRITE "${manifest_file}" "${manifest_contents}")
endfunction()

function(rebuild_standard_library)
    message(STATUS "  Rebuilding bundled Python standard library")

    if(EXISTS "${PYTHON_BUNDLE_DIR}")
        file(REMOVE_RECURSE "${PYTHON_BUNDLE_DIR}")
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        file(MAKE_DIRECTORY "${PYTHON_BUNDLE_DIR}")

        file(COPY "${PYTHON_STDLIB_SRC}/"
            DESTINATION "${PYTHON_LIB_DST}"
            PATTERN "test" EXCLUDE
            PATTERN "tests" EXCLUDE
            PATTERN "__pycache__" EXCLUDE
            PATTERN "*.pyc" EXCLUDE
            PATTERN "*.pyo" EXCLUDE
            PATTERN "idlelib" EXCLUDE
            PATTERN "tkinter" EXCLUDE
            PATTERN "turtledemo" EXCLUDE
            PATTERN "site-packages" EXCLUDE
        )

        if(EXISTS "${PYTHON_PREFIX}/DLLs")
            file(COPY "${PYTHON_PREFIX}/DLLs" DESTINATION "${PYTHON_BUNDLE_DIR}")
        endif()

        file(GLOB PYTHON_DLLS "${PYTHON_PREFIX}/*.dll")
        if(PYTHON_DLLS)
            file(COPY ${PYTHON_DLLS} DESTINATION "${PYTHON_BUNDLE_DIR}")
        endif()
    else()
        file(MAKE_DIRECTORY "${PYTHON_BUNDLE_DIR}/lib")
        file(COPY "${PYTHON_STDLIB_SRC}/"
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
            # Homebrew's development Makefiles and libpython symlinks point
            # outside the stdlib. They are not part of the embedded runtime.
            PATTERN "config-*-darwin" EXCLUDE
            PATTERN "site-packages" EXCLUDE
        )
    endif()

    file(MAKE_DIRECTORY "${SITE_PACKAGES}")
    write_stdlib_manifest("${STDLIB_MANIFEST_FILE}")
endfunction()

function(sync_site_packages)
    execute_process(
        COMMAND "${PYTHON_EXECUTABLE}" -I "${PYTHON_PACKAGE_SYNC_SCRIPT}"
            --requirements-file "${PYTHON_REQUIREMENTS_FILE}"
            --target-site-packages "${SITE_PACKAGES}"
            --manifest-file "${PACKAGE_MANIFEST_FILE}"
        RESULT_VARIABLE sync_result
        OUTPUT_VARIABLE sync_output
        ERROR_VARIABLE sync_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(NOT "${sync_output}" STREQUAL "")
        message(STATUS "  ${sync_output}")
    endif()

    if(NOT sync_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to synchronize bundled Python packages:\n${sync_error}")
    endif()
endfunction()

function(validate_bundle output_result output_error)
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        # Validate with our embedded interpreter and bundled native libraries,
        # never with the developer's Python executable as the runtime under test.
        execute_process(
            COMMAND "${PYTHON_EXECUTABLE}" -I "${PYTHON_NATIVE_BUNDLE_SCRIPT}"
                --app "${APP_BUNDLE_PATH}"
                --executable "${APP_EXECUTABLE}"
                --python-library "${PYTHON_LIBRARY}"
                --check-executable "${PYTHON_CHECK_EXECUTABLE}"
                --launcher "${PYTHON_LAUNCHER}"
            RESULT_VARIABLE validation_result
            OUTPUT_VARIABLE validation_output
            ERROR_VARIABLE validation_error
        )
        if(NOT "${validation_output}" STREQUAL "")
            message(STATUS "${validation_output}")
        endif()
        set(${output_result} "${validation_result}" PARENT_SCOPE)
        set(${output_error} "${validation_error}" PARENT_SCOPE)
        return()
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(path_separator ";")
    else()
        set(path_separator ":")
    endif()

    set(bundle_python_path "${SITE_PACKAGES}${path_separator}${PYTHON_LIB_DST}")

    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "PYTHONHOME=${PYTHON_BUNDLE_DIR}"
            "PYTHONPATH=${bundle_python_path}"
            "PYTHONNOUSERSITE=1"
            "${PYTHON_EXECUTABLE}" -c "import struct; import numpy; import ase.io"
        RESULT_VARIABLE validation_result
        OUTPUT_VARIABLE validation_output
        ERROR_VARIABLE validation_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(NOT "${validation_output}" STREQUAL "")
        message(STATUS "  ${validation_output}")
    endif()

    set(${output_result} "${validation_result}" PARENT_SCOPE)
    set(${output_error} "${validation_error}" PARENT_SCOPE)
endfunction()

message(STATUS "Synchronizing bundled Python runtime...")

query_python(PYTHON_PREFIX "import sys; print(sys.prefix)")
query_python(PYTHON_VERSION "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')")
query_python(PYTHON_FULL_VERSION "import sys; print(sys.version)")
query_python(PYTHON_ARCHITECTURE "import platform; print(platform.machine())")
query_python(PYTHON_STDLIB_SRC "import sysconfig; print(sysconfig.get_path('stdlib'))")
query_python(PYTHON_EXT_SUFFIX "import sysconfig; print(sysconfig.get_config_var('EXT_SUFFIX') or '')")

file(TO_CMAKE_PATH "${PYTHON_EXECUTABLE}" PYTHON_EXECUTABLE)
file(TO_CMAKE_PATH "${PYTHON_PREFIX}" PYTHON_PREFIX)
file(TO_CMAKE_PATH "${PYTHON_STDLIB_SRC}" PYTHON_STDLIB_SRC)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(PYTHON_BUNDLE_DIR "${APP_BUNDLE_PATH}/Contents/Resources/python")
    set(PYTHON_LIB_DST "${PYTHON_BUNDLE_DIR}/lib/python${PYTHON_VERSION}")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(PYTHON_BUNDLE_DIR "${APP_BUNDLE_PATH}/python")
    set(PYTHON_LIB_DST "${PYTHON_BUNDLE_DIR}/Lib")
else()
    set(PYTHON_BUNDLE_DIR "${APP_BUNDLE_PATH}/python")
    set(PYTHON_LIB_DST "${PYTHON_BUNDLE_DIR}/lib/python${PYTHON_VERSION}")
endif()

set(SITE_PACKAGES "${PYTHON_LIB_DST}/site-packages")
set(STDLIB_MANIFEST_FILE "${PYTHON_BUNDLE_DIR}/.atom-studio-stdlib-manifest.cmake")
set(PACKAGE_MANIFEST_FILE "${PYTHON_BUNDLE_DIR}/.atom-studio-package-manifest.json")

set(rebuild_stdlib FALSE)
set(rebuild_reason "")

if(NOT EXISTS "${STDLIB_MANIFEST_FILE}")
    set(rebuild_stdlib TRUE)
    set(rebuild_reason "missing manifest")
elseif(NOT EXISTS "${PYTHON_LIB_DST}")
    set(rebuild_stdlib TRUE)
    set(rebuild_reason "missing standard library directory")
else()
    include("${STDLIB_MANIFEST_FILE}")

    if(NOT "${ATOM_STUDIO_BUNDLED_STDLIB_FORMAT}" STREQUAL "${PYTHON_STDLIB_FORMAT_VERSION}")
        set(rebuild_stdlib TRUE)
        set(rebuild_reason "standard library bundle format changed")
    elseif(NOT "${ATOM_STUDIO_BUNDLED_PYTHON_EXECUTABLE}" STREQUAL "${PYTHON_EXECUTABLE}")
        set(rebuild_stdlib TRUE)
        set(rebuild_reason "Python executable changed")
    elseif(NOT "${ATOM_STUDIO_BUNDLED_PYTHON_PREFIX}" STREQUAL "${PYTHON_PREFIX}")
        set(rebuild_stdlib TRUE)
        set(rebuild_reason "Python prefix changed")
    elseif(NOT "${ATOM_STUDIO_BUNDLED_PYTHON_VERSION}" STREQUAL "${PYTHON_VERSION}")
        set(rebuild_stdlib TRUE)
        set(rebuild_reason "Python version changed")
    elseif(NOT "${ATOM_STUDIO_BUNDLED_PYTHON_FULL_VERSION}" STREQUAL "${PYTHON_FULL_VERSION}")
        set(rebuild_stdlib TRUE)
        set(rebuild_reason "Python build changed")
    elseif(NOT "${ATOM_STUDIO_BUNDLED_PYTHON_ARCHITECTURE}" STREQUAL "${PYTHON_ARCHITECTURE}")
        set(rebuild_stdlib TRUE)
        set(rebuild_reason "Python architecture changed")
    elseif(NOT "${ATOM_STUDIO_BUNDLED_PYTHON_STDLIB_SRC}" STREQUAL "${PYTHON_STDLIB_SRC}")
        set(rebuild_stdlib TRUE)
        set(rebuild_reason "Python stdlib source changed")
    elseif(NOT "${ATOM_STUDIO_BUNDLED_PYTHON_EXT_SUFFIX}" STREQUAL "${PYTHON_EXT_SUFFIX}")
        set(rebuild_stdlib TRUE)
        set(rebuild_reason "Python ABI suffix changed")
    elseif(NOT "${ATOM_STUDIO_BUNDLED_PYTHON_PLATFORM}" STREQUAL "${CMAKE_SYSTEM_NAME}")
        set(rebuild_stdlib TRUE)
        set(rebuild_reason "platform changed")
    endif()
endif()

if(rebuild_stdlib)
    message(STATUS "  Bundled Python standard library is out of date: ${rebuild_reason}")
    rebuild_standard_library()
else()
    message(STATUS "  Bundled Python standard library is up to date")
endif()

file(MAKE_DIRECTORY "${SITE_PACKAGES}")
sync_site_packages()

validate_bundle(validation_result validation_error)
if(NOT validation_result EQUAL 0)
    message(WARNING
        "  Bundled Python validation failed. Rebuilding from scratch.\n${validation_error}")
    rebuild_standard_library()
    sync_site_packages()
    validate_bundle(validation_result validation_error)
    if(NOT validation_result EQUAL 0)
        message(FATAL_ERROR
            "Bundled Python validation failed after rebuild:\n${validation_error}")
    endif()
endif()

file(MAKE_DIRECTORY "${PYTHON_BUNDLE_DIR}/bin" "${PYTHON_BUNDLE_DIR}/atom_studio")
file(COPY "${PYTHON_SHELL_SOURCE}/" DESTINATION "${PYTHON_BUNDLE_DIR}/atom_studio"
    FILES_MATCHING PATTERN "*.py")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    file(COPY_FILE "${PYTHON_LAUNCHER}" "${PYTHON_BUNDLE_DIR}/bin/python3.exe" ONLY_IF_DIFFERENT)
    file(COPY_FILE "${PYTHON_LAUNCHER}" "${PYTHON_BUNDLE_DIR}/bin/python.exe" ONLY_IF_DIFFERENT)
    file(GLOB LAUNCHER_DLLS "${PYTHON_BUNDLE_DIR}/*.dll")
    if(LAUNCHER_DLLS)
        file(COPY ${LAUNCHER_DLLS} DESTINATION "${PYTHON_BUNDLE_DIR}/bin")
    endif()
elseif(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    file(COPY_FILE "${PYTHON_LAUNCHER}" "${PYTHON_BUNDLE_DIR}/bin/python3" ONLY_IF_DIFFERENT)
endif()

write_stdlib_manifest("${STDLIB_MANIFEST_FILE}")
message(STATUS "  Bundled Python runtime is ready")
