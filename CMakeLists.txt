cmake_minimum_required(VERSION 3.22)

# Universal (Apple Silicon + Intel) build, macOS 11 or later
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

project(EXWorkbench VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(EXWorkbench
    COMPANY_NAME "EX Workbench"
    BUNDLE_ID com.exworkbench.plugin
    PLUGIN_MANUFACTURER_CODE Exwb
    PLUGIN_CODE Exw1
    PRODUCT_NAME "EX Workbench"
    FORMATS AU VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    NEEDS_WEB_BROWSER TRUE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Sampler
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_add_binary_data(EXWorkbenchUI SOURCES ui/index.html)
juce_generate_juce_header(EXWorkbench)

target_sources(EXWorkbench PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/engine/Bank.cpp
    Source/engine/Engine.cpp)

target_compile_definitions(EXWorkbench PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=0)

target_link_libraries(EXWorkbench
    PRIVATE
        EXWorkbenchUI
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# offline engine test (not part of the plugin)
add_executable(exw_render_test EXCLUDE_FROM_ALL tests/render_test.cpp Source/engine/Bank.cpp Source/engine/Engine.cpp)

# drives the real processor without a host (Linux/macOS developer check)
add_executable(exw_proc_test EXCLUDE_FROM_ALL tests/proc_test.cpp)
target_link_libraries(exw_proc_test PRIVATE EXWorkbench)
target_include_directories(exw_proc_test PRIVATE $<TARGET_PROPERTY:EXWorkbench,INCLUDE_DIRECTORIES>)
target_compile_definitions(exw_proc_test PRIVATE $<TARGET_PROPERTY:EXWorkbench,COMPILE_DEFINITIONS>)
