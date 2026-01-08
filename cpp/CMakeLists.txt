cmake_minimum_required(VERSION 3.22)
project(pedalboard_cpp LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add JUCE
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/JUCE third_party/JUCE)

# Create a static library for the bridge
add_library(pedalboard_static STATIC
    src/pedalboard.cpp
)

target_include_directories(pedalboard_static PUBLIC
    include
)

# Link JUCE modules
target_link_libraries(pedalboard_static PUBLIC
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_core
    juce::juce_events
    juce::juce_dsp
)

target_compile_definitions(pedalboard_static PUBLIC
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
)

# Ensure it's a static library that can be linked into a shared lib/executable later
set_target_properties(pedalboard_static PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    # Necessary frameworks for JUCE on macOS
    target_link_libraries(pedalboard_static PUBLIC
        "-framework Accelerate"
        "-framework AudioToolbox"
        "-framework AudioUnit"
        "-framework CoreAudio"
        "-framework CoreAudioKit"
        "-framework CoreMIDI"
        "-framework CoreFoundation"
        "-framework Foundation"
        "-framework AppKit"
        "-framework WebKit"
    )
endif()
