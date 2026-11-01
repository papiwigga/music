cmake_minimum_required(VERSION 3.22)

project(AfinaVST VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is downloaded automatically the first time you configure (needs internet + git).
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.8
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(AfinaVST
    COMPANY_NAME                "TuNombre"
    PLUGIN_MANUFACTURER_CODE    Tnom
    PLUGIN_CODE                 Afna
    FORMATS                     VST3 Standalone
    VST3_CATEGORIES             Fx "Pitch Shift"
    PRODUCT_NAME                "AfinaVST"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(AfinaVST PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/dsp/AutotuneEngine.h
    Source/dsp/PitchCorrector.h
    Source/dsp/PsolaShifter.h
    Source/dsp/YinDetector.h)

target_compile_definitions(AfinaVST PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    NOMINMAX
    _USE_MATH_DEFINES
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(AfinaVST
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Optional: DSP unit tests (no JUCE needed to run them).
option(AFINA_BUILD_TESTS "Build the DSP tests" OFF)
if(AFINA_BUILD_TESTS)
    add_executable(dsp_test Tests/dsp_test.cpp)
    target_include_directories(dsp_test PRIVATE Source/dsp)
endif()
