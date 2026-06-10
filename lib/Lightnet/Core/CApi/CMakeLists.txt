# CMake build for the portable animation core + its C ABI.
#
# Produces a static library `anim_core` (the LightnetCore animation player + the C shim) plus a
# host smoke test. The Android NDK (externalNativeBuild) and the iOS framework build both add this
# directory and link `anim_core`; the C header (anim_core_c.h) is the cinterop / JNI surface.
#
# LIGHTNET_CORE_DIR points at lib/Lightnet/Core. It defaults to the in-repo path (this directory's
# parent); when the mobile project consumes this (firmware vendored as a git submodule, or a
# sibling checkout), override it:
#   cmake -S lib/Lightnet/Core/CApi -B build -DLIGHTNET_CORE_DIR=/path/to/lightnet-firmware/lib/Lightnet/Core

cmake_minimum_required(VERSION 3.15)
project(anim_core CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LIGHTNET_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.."
    CACHE PATH "Path to LightnetCore (the lib/Lightnet/Core directory)")

if(NOT EXISTS "${LIGHTNET_CORE_DIR}/Anim/AnimationPlayer.cpp")
    message(FATAL_ERROR "LIGHTNET_CORE_DIR='${LIGHTNET_CORE_DIR}' does not contain Anim/AnimationPlayer.cpp")
endif()

add_library(anim_core STATIC
    anim_core_c.cpp
    "${LIGHTNET_CORE_DIR}/Anim/AnimationPlayer.cpp"
)
# PUBLIC so consumers (smoke test, JNI/cinterop wrappers) inherit the include paths:
#   - this dir for anim_core_c.h
#   - Core/Anim for the player + ProtocolTypes headers
target_include_directories(anim_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${LIGHTNET_CORE_DIR}/Anim"
)

# Host smoke test — proves the C ABI round-trips a packet to the expected colour.
# Off when consumed via add_subdirectory() from a cross-build (Android NDK / iOS), which only
# wants the `anim_core` library, not a host executable.
option(ANIM_CORE_BUILD_TESTS "Build the host smoke test" ON)
if(ANIM_CORE_BUILD_TESTS)
    enable_testing()
    add_executable(anim_core_smoke smoke.cpp)
    target_link_libraries(anim_core_smoke PRIVATE anim_core)
    add_test(NAME smoke COMMAND anim_core_smoke)
endif()
