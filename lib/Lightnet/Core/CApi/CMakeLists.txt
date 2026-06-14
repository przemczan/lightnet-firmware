# CMake build for the portable animation core + its C ABI.
#
# Produces a static library `panel_core` (the LightnetCorePanel animation player + the C shim) plus a
# host smoke test. The Android NDK (externalNativeBuild) and the iOS framework build both add this
# directory and link `panel_core`; the C header (panel_core_c.h) is the cinterop / JNI surface.
#
# LIGHTNET_CORE_DIR points at lib/Lightnet/Core. It defaults to the in-repo path (this directory's
# parent); when the mobile project consumes this (firmware vendored as a git submodule, or a
# sibling checkout), override it:
#   cmake -S lib/Lightnet/Core/CApi -B build -DLIGHTNET_CORE_DIR=/path/to/lightnet-firmware/lib/Lightnet/Core

cmake_minimum_required(VERSION 3.15)
project(panel_core CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LIGHTNET_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.."
    CACHE PATH "Path to LightnetCore (the lib/Lightnet/Core directory)")

if(NOT EXISTS "${LIGHTNET_CORE_DIR}/Panel/AnimationPlayer.cpp")
    message(FATAL_ERROR "LIGHTNET_CORE_DIR='${LIGHTNET_CORE_DIR}' does not contain Panel/AnimationPlayer.cpp")
endif()

add_library(panel_core STATIC
    panel_core_c.cpp
    "${LIGHTNET_CORE_DIR}/Panel/AnimationPlayer.cpp"
)
# PUBLIC so consumers (smoke test, JNI/cinterop wrappers) inherit the include paths:
#   - this dir for panel_core_c.h
#   - Core/Panel for the player (+ ColorCompose)
#   - Core/Common for ProtocolTypes / Palette / LightnetConfig / ColorRef / AnimationTypes
target_include_directories(panel_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${LIGHTNET_CORE_DIR}/Panel"
    "${LIGHTNET_CORE_DIR}/Common"
)

# ----------------------------------------------------------------------------
# controller_core — the whole controller-side scene engine as a second static library.
#
# Where panel_core runs one panel's player, controller_core runs SceneParser -> ScenePlayer ->
# AnimationScheduler -> runners with no hardware, emitting the same wire packets the controller
# would. The mobile app links BOTH: controller_core produces a scene's packets, panel_core renders
# them per panel. The engine lives in Core/Controller/Scene (a library the AVR panel never pulls).
set(LIGHTNET_SCENE_DIR "${LIGHTNET_CORE_DIR}/Controller/Scene" CACHE PATH "Path to lib/Lightnet/Core/Controller/Scene")
set(LIGHTNET_UTILS_DIR "${LIGHTNET_CORE_DIR}/../Utils" CACHE PATH "Path to lib/Lightnet/Utils")

if(NOT EXISTS "${LIGHTNET_SCENE_DIR}/ScenePlayer.cpp")
    message(FATAL_ERROR "LIGHTNET_SCENE_DIR='${LIGHTNET_SCENE_DIR}' does not contain ScenePlayer.cpp")
endif()

add_library(controller_core STATIC
    controller_core_c.cpp
    "${LIGHTNET_SCENE_DIR}/AnimationScheduler.cpp"
    "${LIGHTNET_SCENE_DIR}/AnimationRunner.cpp"
    "${LIGHTNET_SCENE_DIR}/ScenePlayer.cpp"
    "${LIGHTNET_SCENE_DIR}/SceneParser.cpp"
    "${LIGHTNET_CORE_DIR}/Common/ProtocolMeta.cpp"
    "${LIGHTNET_UTILS_DIR}/Crc.cpp"
)
target_include_directories(controller_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"   # controller_core_c.h
    "${LIGHTNET_SCENE_DIR}"         # engine headers
    "${LIGHTNET_CORE_DIR}/Common"   # ProtocolTypes / Palette / LightnetConfig
)

# Host smoke tests — prove the C ABIs round-trip on the host. Off when consumed via
# add_subdirectory() from a cross-build (Android NDK / iOS), which only wants the libraries.
option(ANIM_CORE_BUILD_TESTS "Build the host smoke tests" ON)
if(ANIM_CORE_BUILD_TESTS)
    enable_testing()
    add_executable(panel_core_smoke panel_core_smoke.cpp)
    target_link_libraries(panel_core_smoke PRIVATE panel_core)
    add_test(NAME smoke COMMAND panel_core_smoke)

    add_executable(controller_core_smoke controller_core_smoke.cpp)
    target_link_libraries(controller_core_smoke PRIVATE controller_core)
    add_test(NAME controller_core_smoke COMMAND controller_core_smoke)
endif()
