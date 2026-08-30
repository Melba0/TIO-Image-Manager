cmake_minimum_required(VERSION 3.18)
project(dsl_interpreter VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

# ============================================================
# ONNX Runtime (CPU-only inference backend)
# ============================================================
# Configure the path to the extracted ONNX Runtime release via
# -DONNXRUNTIME_ROOT=<dir>, the ONNXRUNTIME_ROOT env var, or CMAKE_PREFIX_PATH.
# Download (CPU-only Windows / MSVC):
#   https://github.com/microsoft/onnxruntime/releases
#   (onnxruntime-win-x64-<ver>.zip)
if(NOT CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "D:/Visual Studio Data/Modules/onnxruntime-1.29.0/onnxruntime-win-x64-1.29.0" ${CMAKE_PREFIX_PATH})
endif()

find_package(onnxruntime REQUIRED)
if(NOT onnxruntime_FOUND)
    message(FATAL_ERROR "ONNX Runtime not found. "
        "Download onnxruntime-win-x64-<ver>.zip from "
        "https://github.com/microsoft/onnxruntime/releases, extract it and set "
        "-DONNXRUNTIME_ROOT=<extracted-dir>.")
endif()

# ============================================================
# nlohmann/json (header only)
# ============================================================
set(NLOHMANN_INCLUDE_DIR "" CACHE PATH "Directory that contains nlohmann/json.hpp")
if(NOT NLOHMANN_INCLUDE_DIR)
    set(_nlohmann_candidates
        "D:/projects/anaconda/Library/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/nl"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor"
        "C:/ProgramData/anaconda3/Library/include"
        "C:/Users/xm_or/anaconda3/Library/include"
    )
    foreach(_cand ${_nlohmann_candidates})
        if(EXISTS "${_cand}/nlohmann/json.hpp")
            set(NLOHMANN_INCLUDE_DIR "${_cand}")
            break()
        endif()
    endforeach()
endif()
if(NOT NLOHMANN_INCLUDE_DIR)
    message(FATAL_ERROR "nlohmann/json.hpp not found. "
        "Install nlohmann/json (e.g. via anaconda or copy the header into ./nl) "
        "or set -DNLOHMANN_INCLUDE_DIR=<dir-containing-nlohmann>.")
endif()

# ============================================================
# Source files
# ============================================================
set(SOURCES
    src/main.cpp
    src/cache/CacheManager.cpp
    src/cache/CacheIndex.cpp
    src/cache/YoloInference.cpp
    src/scene/SceneInference.cpp
    src/engine/OnnxInference.cpp
    src/parser/Lexer.cpp
    src/parser/Parser.cpp
    src/executor/Context.cpp
    src/executor/Evaluator.cpp
    src/utils/filesystem_utils.cpp
    src/utils/exif_reader.cpp
    src/ExtensionManager.cpp
    src/ModelRegistry.cpp
    src/BuiltinMacros.cpp
)

add_executable(dsl ${SOURCES})

target_include_directories(dsl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${NLOHMANN_INCLUDE_DIR}
)

target_link_libraries(dsl PRIVATE
    onnxruntime::onnxruntime
    gdiplus
    windowscodecs
)

# GDI+ / Windows headers on MSVC: avoid min/max macro clashes with std headers
target_compile_definitions(dsl PRIVATE NOMINMAX)

# Deploy the ONNX Runtime runtime DLL(s) next to the engine executable.
if(WIN32)
    add_custom_command(TARGET dsl POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_INCLUDE_DIR}/../lib/onnxruntime.dll"
                $<TARGET_FILE_DIR:dsl>/onnxruntime.dll
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_INCLUDE_DIR}/../lib/onnxruntime_providers_shared.dll"
                $<TARGET_FILE_DIR:dsl>/onnxruntime_providers_shared.dll
    )
endif()

message(STATUS "onnxruntime include: ${ONNXRUNTIME_INCLUDE_DIR}")
message(STATUS "onnxruntime library: ${ONNXRUNTIME_LIBRARY}")
message(STATUS "nlohmann/json include: ${NLOHMANN_INCLUDE_DIR}")
message(STATUS "Build complete. Run: build/dsl [script.dsl]")
