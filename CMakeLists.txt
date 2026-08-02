cmake_minimum_required(VERSION 3.15)

# Build a universal binary (Intel + Apple Silicon) on macOS. This must be set
# before project() — CMake locks in the target architecture(s) at that point,
# so setting it later has no effect on macOS.
set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Target architectures for macOS" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Target macOS deployment version" FORCE)

project(SampleForge VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    juce
    URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.4.zip
)
FetchContent_MakeAvailable(juce)

# Rubber Band Library (GPL v2+; a commercial licence is available from
# Breakfast Quay for closed-source distribution — see README). It builds with
# Meson, not CMake, so instead of add_subdirectory() we just fetch the source
# and compile its single-file amalgamated build (single/RubberBandSingle.cpp)
# directly into our own target.
FetchContent_Declare(
    rubberband
    GIT_REPOSITORY https://github.com/breakfastquay/rubberband.git
    GIT_TAG v4.0.0
)
FetchContent_MakeAvailable(rubberband)

juce_add_plugin(SampleForge
    COMPANY_NAME "garyanewsome"
    COMPANY_COPYRIGHT "Gary A. Newsome"
    BUNDLE_ID "com.garyanewsome.SampleForge"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE "Gary"
    PLUGIN_CODE "SmpF"
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "SampleForge"
)

juce_generate_juce_header(SampleForge)

target_sources(SampleForge
    PRIVATE
        PluginProcessor.cpp
        PluginProcessor.h
        PluginEditor.cpp
        PluginEditor.h
        SampleForgeLookAndFeel.h
        "${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp"
)

target_include_directories(SampleForge
    PRIVATE
        "${rubberband_SOURCE_DIR}"
)

target_compile_definitions(SampleForge
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
    PRIVATE
        # Apple platforms: use Accelerate's vDSP for FFTs instead of Rubber
        # Band's built-in FFT, and its own resampler implementation.
        HAVE_VDSP=1
        USE_BQRESAMPLER=1
)

find_library(ACCELERATE_FRAMEWORK Accelerate REQUIRED)

target_link_libraries(SampleForge
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        ${ACCELERATE_FRAMEWORK}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
