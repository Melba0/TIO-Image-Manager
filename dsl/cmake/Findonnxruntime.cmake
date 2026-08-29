# Findonnxruntime.cmake - locate the ONNX Runtime C++ SDK (release zip).
#
# ONNX Runtime does not ship a CMake config, so this module searches for the
# extracted release package.  Set ONNXRUNTIME_ROOT to the extracted directory
# (e.g. .../onnxruntime-win-x64-1.29.0) or pass it via CMAKE_PREFIX_PATH.
#
# Download:
#   https://github.com/microsoft/onnxruntime/releases
#   (onnxruntime-win-x64-<ver>.zip for Windows / MSVC)
#
# Exposes the imported target onnxruntime::onnxruntime.

include(FindPackageHandleStandardArgs)

set(ONNXRUNTIME_ROOT "" CACHE PATH "Path to the extracted onnxruntime-win-x64 release directory")

if(NOT ONNXRUNTIME_ROOT)
    foreach(_hint ${CMAKE_PREFIX_PATH})
        if(EXISTS "${_hint}/include/onnxruntime_cxx_api.h")
            set(ONNXRUNTIME_ROOT "${_hint}")
            break()
        endif()
        get_filename_component(_parent "${_hint}" DIRECTORY)
        if(EXISTS "${_parent}/onnxruntime-win-x64-*/include/onnxruntime_cxx_api.h")
            file(GLOB _match "${_parent}/onnxruntime-win-x64-*")
            list(GET _match 0 ONNXRUNTIME_ROOT)
            break()
        endif()
    endforeach()
    if(NOT ONNXRUNTIME_ROOT AND DEFINED ENV{ONNXRUNTIME_ROOT})
        set(ONNXRUNTIME_ROOT "$ENV{ONNXRUNTIME_ROOT}")
    endif()
endif()

find_path(ONNXRUNTIME_INCLUDE_DIR
    NAMES onnxruntime_cxx_api.h
    PATHS "${ONNXRUNTIME_ROOT}/include"
    NO_DEFAULT_PATH)
find_library(ONNXRUNTIME_LIBRARY
    NAMES onnxruntime
    PATHS "${ONNXRUNTIME_ROOT}/lib"
    NO_DEFAULT_PATH)

find_package_handle_standard_args(onnxruntime DEFAULT_MSG
    ONNXRUNTIME_INCLUDE_DIR ONNXRUNTIME_LIBRARY)

if(onnxruntime_FOUND AND NOT TARGET onnxruntime::onnxruntime)
    add_library(onnxruntime::onnxruntime UNKNOWN IMPORTED)
    set_target_properties(onnxruntime::onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}")
endif()

mark_as_advanced(ONNXRUNTIME_ROOT ONNXRUNTIME_INCLUDE_DIR ONNXRUNTIME_LIBRARY)
