cmake_minimum_required(VERSION 3.22)
project(SunLoopsStudio VERSION 0.44.0 LANGUAGES C CXX ASM)

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

# Session log, crash reports and the update check (0.43.0): spdlog (session log), nlohmann/json (GitHub API answers) and
# libcurl (HTTPS; Fedora / Nobara: libcurl-devel). Both libraries are pinned and built in; their headers are SYSTEM so their
# code does not add warnings.
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
    GIT_SHALLOW ON
    SYSTEM
    ${SLS_JUCE_FETCH_OPTIONS}
)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    SYSTEM
    ${SLS_JUCE_FETCH_OPTIONS}
)
FetchContent_MakeAvailable(spdlog nlohmann_json)
set_target_properties(spdlog PROPERTIES POSITION_INDEPENDENT_CODE ON)
find_package(CURL REQUIRED)

# GitHub repository that crash reports go to and updates come from. A token for creating issues can be built in with
# -DSLS_CRASH_REPORT_TOKEN=... (never commit one: anyone with the program can read it); without one the user can paste their
# own in the program, or the report opens in the browser instead.
set(SLS_GITHUB_REPO "Hubert9204/Sun-Loops-Studio" CACHE STRING "owner/repo for crash reports and update checks")
set(SLS_CRASH_REPORT_TOKEN "" CACHE STRING "GitHub token built in for crash reports (empty: none)")

# The start-up sound, built into the program (see resources/sounds/README.md).
juce_add_binary_data(SunLoopsSounds
    NAMESPACE SunLoopsSoundData
    HEADER_NAME SunLoopsSoundData.h
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/resources/sounds/intro.wav")
set_target_properties(SunLoopsSounds PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The sound library (0.22.0): resources/sounds/library (Ogg Vorbis sounds and JSON descriptions, made by tools/sounds) packed into
# one zip archive and linked in with the assembler's .incbin (a C++ array of this size would be slow to compile).
set(SLS_LIBRARY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/sounds/library")
file(GLOB_RECURSE SLS_LIBRARY_FILES CONFIGURE_DEPENDS RELATIVE "${SLS_LIBRARY_DIR}"
    "${SLS_LIBRARY_DIR}/*.ogg" "${SLS_LIBRARY_DIR}/*.json")
list(SORT SLS_LIBRARY_FILES)
set(SLS_LIBRARY_DEPENDS ${SLS_LIBRARY_FILES})
list(TRANSFORM SLS_LIBRARY_DEPENDS PREPEND "${SLS_LIBRARY_DIR}/")
set(SLS_LIBRARY_ZIP "${CMAKE_CURRENT_BINARY_DIR}/sound-library.zip")
add_custom_command(OUTPUT "${SLS_LIBRARY_ZIP}"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${SLS_LIBRARY_ZIP}"
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${SLS_LIBRARY_ZIP}" --format=zip -- ${SLS_LIBRARY_FILES}
    WORKING_DIRECTORY "${SLS_LIBRARY_DIR}"
    DEPENDS ${SLS_LIBRARY_DEPENDS}
    COMMENT "Packing the sound library"
    VERBATIM)
configure_file(src/audio/SoundLibraryData.S.in "${CMAKE_CURRENT_BINARY_DIR}/SoundLibraryData.S" @ONLY)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/SoundLibraryData.S" PROPERTIES OBJECT_DEPENDS "${SLS_LIBRARY_ZIP}")
add_library(SunLoopsLibraryData STATIC "${CMAKE_CURRENT_BINARY_DIR}/SoundLibraryData.S" "${SLS_LIBRARY_ZIP}")
set_target_properties(SunLoopsLibraryData PROPERTIES POSITION_INDEPENDENT_CODE ON LINKER_LANGUAGE C)

# Code shared by the app and the tests (no GUI dependencies).
set(SLS_CORE_SOURCES
    src/audio/Player.cpp
    src/audio/Effects.cpp
    src/audio/BuiltinSamples.cpp
    src/audio/SoundLibrary.cpp
    src/audio/DrumSynth.cpp
    src/audio/MusicTheory.cpp
    src/audio/LoopStarter.cpp
    src/audio/OfflineRenderer.cpp
    src/audio/AudioExport.cpp
    src/audio/AudioImport.cpp
    src/audio/InputRecorder.cpp
    src/audio/MidiImport.cpp
    src/audio/TimeStretch.cpp
    src/audio/SampleEdit.cpp
    src/audio/PitchTrack.cpp
    src/audio/StemSplit.cpp
    src/library/ChannelPreset.cpp
    src/library/DrumKitFile.cpp
    src/library/SoundFont.cpp
    src/library/Library.cpp
    src/library/ProjectFile.cpp
    src/library/FlpFile.cpp
    src/library/FlpProject.cpp
    src/library/HelpIndex.cpp
    src/library/Sfz.cpp
    src/plugins/PluginScanner.cpp
    src/plugins/PluginHost.cpp
    src/support/Logging.cpp
    src/support/CrashHandler.cpp
    src/support/CrashReport.cpp
    src/support/Http.cpp
    src/support/GitHub.cpp
    src/support/UpdateChecker.cpp
)

# LADSPA plugins can be hosted only when the LADSPA header is installed (Fedora / Nobara: ladspa-devel).
include(CheckIncludeFileCXX)
check_include_file_cxx(ladspa.h SLS_HAVE_LADSPA_H)
if(SLS_HAVE_LADSPA_H)
    set(SLS_LADSPA 1)
else()
    set(SLS_LADSPA 0)
    message(STATUS "ladspa.h not found: LADSPA plugins will not be hosted (install ladspa-devel to enable them)")
endif()

set(SLS_COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=1
    JUCE_PLUGINHOST_LADSPA=${SLS_LADSPA})

# The user guides, built into the program for Helios (0.40.0), the offline help.
juce_add_binary_data(SunLoopsHelp HEADER_NAME HelpData.h NAMESPACE HelpData
    SOURCES docs/user-guide.md docs/user-guide.it.md)
set_target_properties(SunLoopsHelp PROPERTIES POSITION_INDEPENDENT_CODE ON)

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
    src/ui/PianoRollToolbar.cpp
    src/ui/ChannelStrip.cpp
    src/ui/PanelWindow.cpp
    src/ui/MixerPanel.cpp
    src/ui/PlaylistTimeline.cpp
    src/ui/PlaylistView.cpp
    src/ui/MainComponent.cpp
    src/ui/MainComponentNotes.cpp
    src/ui/MainComponentPatterns.cpp
    src/ui/MainComponentLoopStarter.cpp
    src/ui/MainComponentImport.cpp
    src/ui/MainComponentRecording.cpp
    src/ui/MainComponentBrowser.cpp
    src/ui/MainComponentProject.cpp
    src/ui/MainComponentAutomation.cpp
    src/ui/MainComponentSampleEditor.cpp
    src/ui/MainComponentDrums.cpp
    src/ui/MainComponentChannels.cpp
    src/ui/MainComponentHistory.cpp
    src/ui/MainComponentSafety.cpp
    src/ui/MainComponentSampler.cpp
    src/ui/MainComponentPlugins.cpp
    src/ui/MainComponentSynth.cpp
    src/ui/SynthPanel.cpp
    src/ui/DrumMachine.cpp
    src/ui/SamplerPanel.cpp
    src/ui/AutomationEditor.cpp
    src/ui/PluginParameterPicker.cpp
    src/ui/SlicerWindow.cpp
    src/ui/MainComponentSlicer.cpp
    src/ui/PitchEditor.cpp
    src/ui/MainComponentPitch.cpp
    src/ui/MainComponentStems.cpp
    src/ui/HeliosWindow.cpp
    src/ui/MainComponentHelios.cpp
    src/ui/MainComponentSupport.cpp
    src/ui/CrashReportDialog.cpp
    src/ui/MetersView.cpp
    src/ui/SampleEditor.cpp
    ${SLS_CORE_SOURCES})

target_include_directories(SunLoopsStudio PRIVATE src)
target_include_directories(SunLoopsStudio SYSTEM PRIVATE third_party/signalsmith third_party/tinysoundfont)

target_compile_definitions(SunLoopsStudio PRIVATE
    ${SLS_COMMON_DEFINITIONS}
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SunLoopsStudio,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
    SLS_GITHUB_REPO="${SLS_GITHUB_REPO}"
    SLS_CRASH_REPORT_TOKEN="${SLS_CRASH_REPORT_TOKEN}")
# Function names in crash stack traces (they survive stripping): the program's own and JUCE's symbols are exported.
target_link_options(SunLoopsStudio PRIVATE "LINKER:--export-dynamic-symbol=_ZN3sls*" "LINKER:--export-dynamic-symbol=_ZN4juce*")

target_link_libraries(SunLoopsStudio PRIVATE
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    CURL::libcurl
    SunLoopsSounds
    SunLoopsLibraryData
    SunLoopsHelp
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
    tests/test_support.cpp
    ${SLS_CORE_SOURCES})

target_include_directories(SunLoopsStudioTests PRIVATE src)
target_include_directories(SunLoopsStudioTests SYSTEM PRIVATE third_party/signalsmith third_party/tinysoundfont)
target_compile_definitions(SunLoopsStudioTests PRIVATE ${SLS_COMMON_DEFINITIONS})
target_link_options(SunLoopsStudioTests PRIVATE "LINKER:--export-dynamic-symbol=_ZN3sls*")

target_link_libraries(SunLoopsStudioTests PRIVATE
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    CURL::libcurl
    SunLoopsSounds
    SunLoopsLibraryData
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# Two tiny plugins (VST3 and LV2) that the tests load to check the plugin host with real binaries. Not installed.
option(SLS_BUILD_TEST_PLUGINS "Build the VST3 / LV2 test plugins used by the tests" ON)
if(SLS_BUILD_TEST_PLUGINS)
    foreach(kind Synth Gain)
        set(target SlsTest${kind})
        if(kind STREQUAL "Synth")
            set(isSynth TRUE)
            set(code Slsy)
        else()
            set(isSynth FALSE)
            set(code Slsg)
        endif()
        juce_add_plugin(${target}
            COMPANY_NAME "Sun Loops Studio tests"
            PRODUCT_NAME "SLS Test ${kind}"
            PLUGIN_MANUFACTURER_CODE Slst
            PLUGIN_CODE ${code}
            IS_SYNTH ${isSynth}
            NEEDS_MIDI_INPUT ${isSynth}
            FORMATS VST3 LV2
            LV2URI "urn:sun-loops-studio:test-${kind}"
            COPY_PLUGIN_AFTER_BUILD FALSE)
        target_sources(${target} PRIVATE tests/plugins/TestPlugins.cpp)
        target_compile_definitions(${target} PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
                                   $<$<STREQUAL:${kind},Synth>:SLS_TEST_SYNTH=1>)
        target_link_libraries(${target} PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
        add_dependencies(SunLoopsStudioTests ${target}_VST3 ${target}_LV2)
    endforeach()
    target_compile_definitions(SunLoopsStudioTests PRIVATE
        SLS_TEST_PLUGIN_DIR="${CMAKE_BINARY_DIR}")
endif()

enable_testing()
add_test(NAME core COMMAND SunLoopsStudioTests)
