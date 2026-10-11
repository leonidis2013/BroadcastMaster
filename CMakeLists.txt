cmake_minimum_required(VERSION 3.22)

# Static MSVC runtime so the plugin does not need the Visual C++ redistributable on the streaming PC.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
# Apple universal binary (Apple silicon + Intel) by default; Windows builds are x64.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")

project(BroadcastMaster VERSION 0.4.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BM_BUILD_TESTS "Build the standalone metering and DSP tests" OFF)

# JUCE is fetched at configure time. Set JUCE_DIR_OVERRIDE to use a local checkout instead.
if (JUCE_DIR_OVERRIDE)
    add_subdirectory(${JUCE_DIR_OVERRIDE} JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.8
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(BroadcastMaster
    COMPANY_NAME "BroadcastMaster"
    PLUGIN_MANUFACTURER_CODE BcMs
    PLUGIN_CODE BcMa
    FORMATS VST3
    PRODUCT_NAME "BroadcastMaster"
    VST3_CATEGORIES Fx Mastering Analyzer
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(BroadcastMaster PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(BroadcastMaster PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(BroadcastMaster
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if (BM_BUILD_TESTS)
    enable_testing()
    add_executable(MeterTests Tests/MeterTests.cpp)
    target_include_directories(MeterTests PRIVATE Source/Meters)
    add_test(NAME MeterTests COMMAND MeterTests)

    add_executable(DspTests Tests/DspTests.cpp)
    target_include_directories(DspTests PRIVATE Source)
    add_test(NAME DspTests COMMAND DspTests)

    add_executable(ChainTests Tests/ChainTests.cpp)
    target_include_directories(ChainTests PRIVATE Source)
    add_test(NAME ChainTests COMMAND ChainTests)
endif()
