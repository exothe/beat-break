cmake_minimum_required(VERSION 3.22)

project(BeatBreak VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")

    # Universal binary: hosts running under Rosetta (FL Studio can be launched
    # either way) cannot load an arm64-only plugin.
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures")
endif()

# ---------------------------------------------------------------------------
# JUCE: use a local checkout if JUCE_PATH is given, otherwise fetch 8.0.15.
# ---------------------------------------------------------------------------
if (DEFINED JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(STATUS "Using local JUCE at ${JUCE_PATH}")
    add_subdirectory("${JUCE_PATH}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(BeatBreak
    COMPANY_NAME              "BeatBreak Audio"
    BUNDLE_ID                 "com.beatbreak.beatbreak"
    PLUGIN_MANUFACTURER_CODE  Bbrk
    PLUGIN_CODE               Bbt1
    FORMATS                   VST3 AU Standalone
    PRODUCT_NAME              "BeatBreak"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD   TRUE)

target_sources(BeatBreak PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/GrossEngine.cpp
    Source/CurveEditor.cpp
    Source/FactoryPatterns.cpp)

target_compile_definitions(BeatBreak PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(BeatBreak PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline DSP checks: cmake --build build --target BeatBreakTests && run it
# ---------------------------------------------------------------------------
juce_add_console_app(BeatBreakTests PRODUCT_NAME "BeatBreakTests")

target_sources(BeatBreakTests PRIVATE
    Tests/EngineTests.cpp
    Source/GrossEngine.cpp
    Source/FactoryPatterns.cpp)

target_compile_definitions(BeatBreakTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(BeatBreakTests PRIVATE
    juce::juce_audio_basics
    juce::juce_core
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
