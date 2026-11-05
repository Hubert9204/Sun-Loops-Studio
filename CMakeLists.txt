cmake_minimum_required(VERSION 3.22)
project(SunLoopsStudio VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# JUCE is downloaded at configure time (needs network once). Pin the tag so builds are reproducible.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(JUCE)

# Code shared by the app and the tests (no GUI dependencies).
set(SLS_CORE_SOURCES
    src/audio/Player.cpp
    src/audio/BuiltinSamples.cpp
    src/audio/OfflineRenderer.cpp
    src/plugins/PluginScanner.cpp)

set(SLS_COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1)

# --- Application -------------------------------------------------------------
juce_add_gui_app(SunLoopsStudio
    PRODUCT_NAME "Sun Loops Studio"
    COMPANY_NAME "Sun Loops"
    BUNDLE_ID "com.sunloops.sunloopsstudio"
    ICON_BIG   "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app_icon_256.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app_icon_16.png")

target_sources(SunLoopsStudio PRIVATE
    src/main.cpp
    src/audio/AudioEngine.cpp
    src/ui/AudioMeter.cpp
    src/ui/PianoRoll.cpp
    src/ui/ChannelStrip.cpp
    src/ui/MainComponent.cpp
    ${SLS_CORE_SOURCES})

target_include_directories(SunLoopsStudio PRIVATE src)

target_compile_definitions(SunLoopsStudio PRIVATE
    ${SLS_COMMON_DEFINITIONS}
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SunLoopsStudio,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")

target_link_libraries(SunLoopsStudio PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# --- Headless tests ----------------------------------------------------------
juce_add_console_app(SunLoopsStudioTests
    PRODUCT_NAME "SunLoopsStudioTests")

target_sources(SunLoopsStudioTests PRIVATE
    tests/test_core.cpp
    ${SLS_CORE_SOURCES})

target_include_directories(SunLoopsStudioTests PRIVATE src)
target_compile_definitions(SunLoopsStudioTests PRIVATE ${SLS_COMMON_DEFINITIONS})

target_link_libraries(SunLoopsStudioTests PRIVATE
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

enable_testing()
add_test(NAME core COMMAND SunLoopsStudioTests)
