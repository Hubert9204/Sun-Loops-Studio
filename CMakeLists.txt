cmake_minimum_required(VERSION 3.22)
# macOS (0.63.0): the oldest system the program runs on. Must be set before project(). A universal build (Apple silicon and
# Intel) is configured with -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" (scripts/build_macos.sh does).
# 10.15 Catalina (0.63.0): the newest macOS of Intel Macs from 2012 (Mac mini, iMac, MacBook Pro). The Apple silicon half of the
# app needs macOS 11 in any case (the first macOS for those Macs); the compiler refuses any call that Catalina does not have.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Oldest macOS the program runs on")
project(SunLoopsStudio VERSION 0.64.0 LANGUAGES C CXX ASM)

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
    GIT_TAG v1.15.3 # (0.63.0: 1.14 bundles a fmt that recent Apple compilers reject)
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
    src/audio/Convolver.cpp
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
    src/library/DemoSong.cpp
    src/library/FlpFile.cpp
    src/library/FlpProject.cpp
    src/library/HelpIndex.cpp
    src/library/Sfz.cpp
    src/assistant/HeliosChat.cpp
    src/plugins/PluginScanner.cpp
    src/plugins/PluginHost.cpp
    src/support/Logging.cpp
    src/support/CrashHandler.cpp
    src/support/CrashReport.cpp
    src/support/ProcessMemory.cpp
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
    JUCE_PLUGINHOST_LADSPA=${SLS_LADSPA}
    $<$<PLATFORM_ID:Darwin>:JUCE_PLUGINHOST_AU=1>)

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
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app_icon_16.png"
    # macOS (0.63.0): recording asks for the microphone; the text is what the system's question shows.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Sun Loops Studio records audio from your microphone or audio interface when you press Record."
    DOCUMENT_EXTENSIONS slp)

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
    src/ui/StepGraph.cpp
    src/ui/PanelWindow.cpp
    src/ui/MixerPanel.cpp
    src/ui/PlaylistTimeline.cpp
    src/ui/PlaylistView.cpp
    src/ui/PluginPicker.cpp
    src/ui/MainComponentToolbar.cpp
    src/ui/MainComponent.cpp
    src/ui/MainComponentNotes.cpp
    src/ui/MainComponentPerformance.cpp
    src/ui/PerformancePanel.cpp
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
    src/assistant/HeliosAssistant.cpp
    src/ui/MainComponentSupport.cpp
    src/ui/MainComponentChains.cpp
    src/ui/MainComponentTempo.cpp
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
# (GNU ld only; macOS keeps the names of an executable's functions by itself.)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(SunLoopsStudio PRIVATE "LINKER:--export-dynamic-symbol=_ZN3sls*" "LINKER:--export-dynamic-symbol=_ZN4juce*")
endif()

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

# --- Helios LLM mode (0.64.0): sun-loops-helios, the helper that runs the language model ------------------------------
# tools/helios-llm is built as a project of its own, once per processor (llama.cpp cannot be built for Apple silicon and Intel in
# one configuration), always optimised (a model in a debug build is unusably slow); on a Mac the halves are joined with lipo.
# The helper is put next to the program (on a Mac: Contents/MacOS), where Helios looks for it. With -DSLS_HELIOS_LLM=OFF the
# program is built without it and Helios's "LLM mode" says so.
option(SLS_HELIOS_LLM "Build sun-loops-helios, the helper that runs Helios's language model (llama.cpp)" ON)
if(SLS_HELIOS_LLM)
    include(ExternalProject)
    if(APPLE AND CMAKE_OSX_ARCHITECTURES)
        set(SLS_HELIOS_ARCHS ${CMAKE_OSX_ARCHITECTURES})
    else()
        set(SLS_HELIOS_ARCHS native)
    endif()
    set(SLS_HELIOS_EXE "sun-loops-helios${CMAKE_EXECUTABLE_SUFFIX}")
    set(SLS_HELIOS_PARTS "")
    set(SLS_HELIOS_TARGETS "")
    set(generatorPlatform "")
    if(CMAKE_GENERATOR_PLATFORM) # Visual Studio: x64 / ARM64
        set(generatorPlatform CMAKE_GENERATOR_PLATFORM "${CMAKE_GENERATOR_PLATFORM}")
    endif()
    foreach(arch IN LISTS SLS_HELIOS_ARCHS)
        set(args
            -DCMAKE_BUILD_TYPE=Release
            -DSLS_VERSION=${PROJECT_VERSION}
            -DSLS_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DFETCHCONTENT_SOURCE_DIR_JUCE=${juce_SOURCE_DIR}
            -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
            -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
        if(FETCHCONTENT_SOURCE_DIR_LLAMA) # a local llama.cpp checkout instead of downloading it
            list(APPEND args -DFETCHCONTENT_SOURCE_DIR_LLAMA=${FETCHCONTENT_SOURCE_DIR_LLAMA})
        endif()
        if(APPLE AND NOT arch STREQUAL "native")
            list(APPEND args -DCMAKE_OSX_ARCHITECTURES=${arch})
            if(arch STREQUAL "arm64")
                list(APPEND args -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0) # the first macOS of Apple silicon (Metal needs it)
            else()
                list(APPEND args -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET})
            endif()
        endif()
        set(part "${CMAKE_BINARY_DIR}/helios-${arch}/SunLoopsHelios_artefacts/Release/${SLS_HELIOS_EXE}")
        ExternalProject_Add(SunLoopsHelios_${arch}
            SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/helios-llm"
            BINARY_DIR "${CMAKE_BINARY_DIR}/helios-${arch}"
            CMAKE_GENERATOR "${CMAKE_GENERATOR}"
            ${generatorPlatform}
            CMAKE_ARGS ${args}
            BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --config Release --target SunLoopsHelios
            INSTALL_COMMAND ""
            BUILD_ALWAYS ON # (only what changed is rebuilt)
            BUILD_BYPRODUCTS "${part}")
        list(APPEND SLS_HELIOS_PARTS "${part}")
        list(APPEND SLS_HELIOS_TARGETS SunLoopsHelios_${arch})
    endforeach()

    set(SLS_HELIOS_HELPER "${CMAKE_BINARY_DIR}/helios/${SLS_HELIOS_EXE}")
    list(LENGTH SLS_HELIOS_PARTS partCount)
    if(partCount GREATER 1)
        set(joinCommand lipo -create ${SLS_HELIOS_PARTS} -output "${SLS_HELIOS_HELPER}")
    else()
        set(joinCommand "${CMAKE_COMMAND}" -E copy ${SLS_HELIOS_PARTS} "${SLS_HELIOS_HELPER}")
    endif()
    add_custom_command(OUTPUT "${SLS_HELIOS_HELPER}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/helios"
        COMMAND ${joinCommand}
        DEPENDS ${SLS_HELIOS_TARGETS} ${SLS_HELIOS_PARTS}
        COMMENT "Joining sun-loops-helios"
        VERBATIM)
    add_custom_target(SunLoopsHeliosHelper ALL DEPENDS "${SLS_HELIOS_HELPER}")
    add_dependencies(SunLoopsStudio SunLoopsHeliosHelper)
    add_custom_command(TARGET SunLoopsStudio POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy "${SLS_HELIOS_HELPER}" "$<TARGET_FILE_DIR:SunLoopsStudio>/${SLS_HELIOS_EXE}"
        VERBATIM)
    target_compile_definitions(SunLoopsStudio PRIVATE SLS_HELIOS_LLM=1)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        include(GNUInstallDirs)
        install(PROGRAMS "${SLS_HELIOS_HELPER}" DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(FILES third_party/llama.cpp/LICENSE-llama.cpp.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/sun-loops-studio)
    endif()
else()
    target_compile_definitions(SunLoopsStudio PRIVATE SLS_HELIOS_LLM=0)
endif()

# --- Installation (cmake --install build --prefix ~/.local) -------------------
if(CMAKE_SYSTEM_NAME STREQUAL "Linux") # (macOS: the .app made by scripts/build_macos.sh)
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
endif()

# --- SunBurst: the synthesizer plugin (0.48.0), VST3 and Standalone --------------------------------------------
set(SUNBURST_SOURCES
    src/sunburst/SunBurstParams.cpp
    src/sunburst/Wavetables.cpp
    src/sunburst/Voice.cpp
    src/sunburst/Fx.cpp
    src/sunburst/Presets.cpp
    src/sunburst/SunBurstProcessor.cpp
    src/synthkit/SynthProcessor.cpp)

option(SLS_BUILD_SUNBURST "Build the SunBurst synthesizer plugin (VST3 and Standalone)" ON)
if(SLS_BUILD_SUNBURST)
    juce_add_plugin(SunBurst
        COMPANY_NAME "Sun Loops Studio"
        PRODUCT_NAME "SunBurst"
        VERSION ${PROJECT_VERSION}
        PLUGIN_MANUFACTURER_CODE Sunl
        PLUGIN_CODE Sbst
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        FORMATS VST3 Standalone
        VST3_CATEGORIES Instrument Synth
        COPY_PLUGIN_AFTER_BUILD FALSE)
    target_sources(SunBurst PRIVATE ${SUNBURST_SOURCES} plugins/common/HardwareUi.cpp plugins/common/SynthUi.cpp plugins/sunburst/SunBurstEditor.cpp plugins/sunburst/PluginMain.cpp)
    target_include_directories(SunBurst PRIVATE src plugins)
    target_compile_definitions(SunBurst PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(SunBurst PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    # Installed next to the program: Sun Loops Studio finds it in ../lib/vst3 (also inside the AppImage).
    install(DIRECTORY "${CMAKE_BINARY_DIR}/SunBurst_artefacts/$<CONFIG>/VST3/SunBurst.vst3"
            DESTINATION lib/vst3
            USE_SOURCE_PERMISSIONS)
    add_dependencies(SunLoopsStudio SunBurst_VST3)
endif()

# --- HexForge: the hybrid FM + subtractive synthesizer plugin (0.50.0) ----------------------------------------------
set(HEXFORGE_SOURCES
    src/hexforge/HexParams.cpp
    src/hexforge/HexWaves.cpp
    src/hexforge/HexVoice.cpp
    src/hexforge/HexPresets.cpp
    src/hexforge/HexProcessor.cpp)

option(SLS_BUILD_HEXFORGE "Build the HexForge synthesizer plugin (VST3 and Standalone)" ON)
if(SLS_BUILD_HEXFORGE)
    juce_add_plugin(HexForge
        COMPANY_NAME "Sun Loops Studio"
        PRODUCT_NAME "HexForge"
        VERSION ${PROJECT_VERSION}
        PLUGIN_MANUFACTURER_CODE Sunl
        PLUGIN_CODE Hxfg
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        FORMATS VST3 Standalone
        VST3_CATEGORIES Instrument Synth
        COPY_PLUGIN_AFTER_BUILD FALSE)
    # It shares SunBurst's band-limited tables, building blocks and effects, and the synthkit plugin base (0.51.0).
    target_sources(HexForge PRIVATE ${HEXFORGE_SOURCES} src/sunburst/Wavetables.cpp src/sunburst/Fx.cpp src/synthkit/SynthProcessor.cpp
                   plugins/common/HardwareUi.cpp plugins/common/SynthUi.cpp plugins/hexforge/HexEditor.cpp plugins/hexforge/PluginMain.cpp)
    target_include_directories(HexForge PRIVATE src plugins)
    target_compile_definitions(HexForge PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(HexForge PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    install(DIRECTORY "${CMAKE_BINARY_DIR}/HexForge_artefacts/$<CONFIG>/VST3/HexForge.vst3"
            DESTINATION lib/vst3
            USE_SOURCE_PERMISSIONS)
    add_dependencies(SunLoopsStudio HexForge_VST3)
endif()

# --- Headless tests ----------------------------------------------------------
juce_add_console_app(SunLoopsStudioTests
    PRODUCT_NAME "SunLoopsStudioTests")

target_sources(SunLoopsStudioTests PRIVATE
    tests/test_core.cpp
    tests/test_support.cpp
    tests/test_sunburst.cpp
    tests/test_hexforge.cpp
    tests/test_helios.cpp
    src/ui/PianoRoll.cpp # 0.58.0: the spare quarter is tested through its public functions
    ${SLS_CORE_SOURCES}
    ${SUNBURST_SOURCES}
    ${HEXFORGE_SOURCES})

target_include_directories(SunLoopsStudioTests PRIVATE src)
target_include_directories(SunLoopsStudioTests SYSTEM PRIVATE third_party/signalsmith third_party/tinysoundfont)
target_compile_definitions(SunLoopsStudioTests PRIVATE ${SLS_COMMON_DEFINITIONS} SLS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(SunLoopsStudioTests PRIVATE "LINKER:--export-dynamic-symbol=_ZN3sls*")
endif()
if(APPLE) # the Audio Unit host's generic editor (a window the tests never open, but the code is linked)
    target_link_libraries(SunLoopsStudioTests PRIVATE "-framework CoreAudioKit")
endif()

target_link_libraries(SunLoopsStudioTests PRIVATE
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    CURL::libcurl
    SunLoopsSounds
    SunLoopsLibraryData
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_dsp
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

if(SLS_BUILD_SUNBURST)
    add_dependencies(SunLoopsStudioTests SunBurst_VST3)
endif()
if(SLS_BUILD_HEXFORGE)
    add_dependencies(SunLoopsStudioTests HexForge_VST3)
endif()

enable_testing()
add_test(NAME core COMMAND SunLoopsStudioTests)
