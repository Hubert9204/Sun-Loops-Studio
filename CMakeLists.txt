cmake_minimum_required(VERSION 3.22)
project(SunLoopsStudio VERSION 0.10.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# JUCE is downloaded at configure time (needs network once). Pin the tag so builds are reproducible.
include(FetchContent)
# JUCE ships install rules for itself (headers, cmake files, tools). EXCLUDE_FROM_ALL (CMake >= 3.28) keeps
# them out of `cmake --install`, so only this application is installed.
set(SLS_JUCE_FETCH_OPTIONS "")
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    set(SLS_JUCE_FETCH_OPTIONS EXCLUDE_FROM_ALL)
endif()
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON
    ${SLS_JUCE_FETCH_OPTIONS}
)
FetchContent_MakeAvailable(JUCE)

# Sounds built into the program: the Cyber-Hub theme and the start-up sound (used unchanged, see resources/sounds/README.md).
file(GLOB SLS_SOUND_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/sounds/cyber-hub/*.oga"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/sounds/intro.wav")
juce_add_binary_data(SunLoopsSounds
    NAMESPACE SunLoopsSoundData
    HEADER_NAME SunLoopsSoundData.h
    SOURCES ${SLS_SOUND_FILES})
set_target_properties(SunLoopsSounds PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Code shared by the app and the tests (no GUI dependencies).
set(SLS_CORE_SOURCES
    src/audio/Player.cpp
    src/audio/Effects.cpp
    src/audio/BuiltinSamples.cpp
    src/audio/CyberHub.cpp
    src/audio/OfflineRenderer.cpp
    src/audio/AudioExport.cpp
    src/audio/AudioImport.cpp
    src/audio/InputRecorder.cpp
    src/audio/MidiImport.cpp
    src/library/ChannelPreset.cpp
    src/library/Library.cpp
    src/library/ProjectFile.cpp
    src/plugins/PluginScanner.cpp)

set(SLS_COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
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
    src/ui/AudioSettings.cpp
    src/ui/MetronomePanel.cpp
    src/ui/AssignTrackDialog.cpp
    src/ui/VuMeter.cpp
    src/ui/Knob.cpp
    src/ui/FileBrowser.cpp
    src/ui/TrackSelector.cpp
    src/ui/PianoRoll.cpp
    src/ui/ChannelStrip.cpp
    src/ui/PanelWindow.cpp
    src/ui/MixerPanel.cpp
    src/ui/PlaylistTimeline.cpp
    src/ui/PlaylistView.cpp
    src/ui/MainComponent.cpp
    src/ui/MainComponentNotes.cpp
    src/ui/MainComponentPatterns.cpp
    src/ui/MainComponentImport.cpp
    src/ui/MainComponentRecording.cpp
    src/ui/MainComponentBrowser.cpp
    src/ui/MainComponentProject.cpp
    ${SLS_CORE_SOURCES})

target_include_directories(SunLoopsStudio PRIVATE src)

target_compile_definitions(SunLoopsStudio PRIVATE
    ${SLS_COMMON_DEFINITIONS}
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SunLoopsStudio,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")

target_link_libraries(SunLoopsStudio PRIVATE
    SunLoopsSounds
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# --- Installation (cmake --install build --prefix ~/.local) -------------------
include(GNUInstallDirs)
# The product name contains spaces, so the installed command is called sun-loops-studio.
install(PROGRAMS "$<TARGET_FILE:SunLoopsStudio>"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        RENAME sun-loops-studio)
install(FILES resources/linux/sun-loops-studio.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES resources/linux/sun-loops-studio.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
install(FILES resources/icons/app_icon_256.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
        RENAME sun-loops-studio.png)
install(FILES resources/icons/app_icon_16.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/16x16/apps
        RENAME sun-loops-studio.png)

# --- Headless tests ----------------------------------------------------------
juce_add_console_app(SunLoopsStudioTests
    PRODUCT_NAME "SunLoopsStudioTests")

target_sources(SunLoopsStudioTests PRIVATE
    tests/test_core.cpp
    ${SLS_CORE_SOURCES})

target_include_directories(SunLoopsStudioTests PRIVATE src)
target_compile_definitions(SunLoopsStudioTests PRIVATE ${SLS_COMMON_DEFINITIONS})

target_link_libraries(SunLoopsStudioTests PRIVATE
    SunLoopsSounds
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

enable_testing()
add_test(NAME core COMMAND SunLoopsStudioTests)
