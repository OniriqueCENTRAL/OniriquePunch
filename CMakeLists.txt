cmake_minimum_required(VERSION 3.22)

project(OniriquePunch VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    # Universal binary, and the oldest macOS the JUCE 9 runtime supports.
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# JUCE is looked for next to this file first, then in the PitchBox checkout
# beside this one, so the plugins of the range can share a single copy.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    set(PUNCH_JUCE_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
else()
    set(PUNCH_JUCE_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/../PitchBox/JUCE")
endif()

set(PUNCH_JUCE_DIR "${PUNCH_JUCE_DEFAULT}" CACHE PATH "Path to JUCE")

if(NOT EXISTS "${PUNCH_JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE not found at ${PUNCH_JUCE_DIR}.\n"
        "Run:  git clone --depth 1 --branch 9.0.3 https://github.com/juce-framework/JUCE.git")
endif()

add_subdirectory("${PUNCH_JUCE_DIR}" JUCE EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# The DSP, built on its own so the offline test can link it without JUCE.
add_library(punch_core STATIC Source/dsp/Punch.cpp)
target_include_directories(punch_core PUBLIC Source)
set_target_properties(punch_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_definitions(punch_core PUBLIC _USE_MATH_DEFINES)
    target_compile_options(punch_core PRIVATE /W4)
else()
    target_compile_options(punch_core PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# The plugin.
set(PUNCH_FORMATS VST3)

if(APPLE)
    list(APPEND PUNCH_FORMATS AU)
endif()

# The system VST3 folder on Windows is under Program Files, which a normal build
# cannot write to, so installing after the build is opt-in there.
if(WIN32)
    option(PUNCH_COPY_AFTER_BUILD "Install the plugin after building" OFF)
else()
    option(PUNCH_COPY_AFTER_BUILD "Install the plugin after building" ON)
endif()

juce_add_plugin(OniriquePunch
    COMPANY_NAME "Onirique"
    BUNDLE_ID "com.onirique.punch"
    PLUGIN_MANUFACTURER_CODE Onrq
    PLUGIN_CODE Pnch
    FORMATS ${PUNCH_FORMATS}
    PRODUCT_NAME "OniriquePunch"
    PLUGIN_NAME "ONIRIQUE Punch"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Dynamics
    COPY_PLUGIN_AFTER_BUILD ${PUNCH_COPY_AFTER_BUILD})

juce_add_binary_data(PunchAssets SOURCES Resources/sigil.png)

target_sources(OniriquePunch PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

# The licence every Onirique plugin carries: the folder is copied as it is from
# plugin-license/ in the ONIRIQUE CENTRAL repository, and is only ever changed there.
include(License/OniriqueLicense.cmake)
onirique_add_license(OniriquePunch)

target_compile_definitions(OniriquePunch PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

if(MSVC)
    target_compile_options(OniriquePunch PRIVATE /utf-8)
endif()

target_link_libraries(OniriquePunch
    PRIVATE
        punch_core
        PunchAssets
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline test: the DSP without a host.
enable_testing()

add_executable(test_punch tests/test_punch.cpp)
target_link_libraries(test_punch PRIVATE punch_core)
add_test(NAME punch COMMAND test_punch)

# A development tool, not part of the product: renders the editor to a PNG
# and runs the plugin as a host would. Built only when asked for by name.
add_executable(punch_snapshot EXCLUDE_FROM_ALL tools/snapshot.cpp)

target_include_directories(punch_snapshot PRIVATE
    $<TARGET_PROPERTY:OniriquePunch,INCLUDE_DIRECTORIES>)

target_compile_definitions(punch_snapshot PRIVATE
    $<TARGET_PROPERTY:OniriquePunch,COMPILE_DEFINITIONS>)

if(MSVC)
    target_compile_options(punch_snapshot PRIVATE /utf-8)
endif()

target_link_libraries(punch_snapshot PRIVATE OniriquePunch punch_core PunchAssets)
