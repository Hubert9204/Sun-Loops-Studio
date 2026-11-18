cmake_minimum_required(VERSION 3.22)
# macOS (0.63.0): the oldest system the program runs on. Must be set before project(). A universal build (Apple silicon and
# Intel) is configured with -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" (scripts/build_macos.sh does).
# 10.15 Catalina (0.63.0): the newest macOS of Intel Macs from 2012 (Mac mini, iMac, MacBook Pro). The Apple silicon half of the
# app needs macOS 11 in any case (the first macOS for those Macs); the compiler refuses any call that Catalina does not have.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Oldest macOS the program runs on")
# Windows (0.75.0): the C and C++ runtime is linked into every program and plugin (/MT), so nothing has to be installed beside
# them (no Visual C++ Redistributable). Must be set before project().
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "The MSVC runtime: static")
project(SunLoopsStudio VERSION 0.81.0 LANGUAGES C CXX)
# The assembler links the sound library in on Linux and macOS; Windows (0.75.0) uses a resource instead (see below).
if(NOT WIN32)
    enable_language(ASM)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Windows (0.75.0): built with clang-cl (Visual Studio's Clang, the compiler the continuous integration and the releases use) or
# MSVC. windows.h without its min / max macros (but whole: JUCE needs shellapi.h and the rest), M_PI and friends from <cmath>, the C library's plain functions (getenv...)
# without "deprecated" warnings; the sources are UTF-8 (Italian text).
if(WIN32)
    add_compile_definitions(NOMINMAX= _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS) # (NOMINMAX empty, as JUCE defines it again)
    if(MSVC)
        add_compile_options(/utf-8 $<$<CXX_COMPILER_ID:MSVC>:/bigobj> $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>)
        # Function names for the crash reports' stack traces: a .pdb next to each program, also in Release builds (with Clang only
        # the names and line tables, which keeps it small). /OPT:REF and /OPT:ICF are what a Release build has without /DEBUG.
        add_compile_options("$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:Clang>>:-gline-tables-only>"
                            "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:/Zi>")
        add_link_options("$<$<CONFIG:Release>:/DEBUG>" "$<$<CONFIG:Release>:/OPT:REF>" "$<$<CONFIG:Release>:/OPT:ICF>")
        # 8 MB of stack for every thread of a program, as Linux and macOS give the main thread (Windows: 1 MB). Only reserved:
        # memory is used as the stack grows. (Ignored for the plugins, which are DLLs.)
        add_link_options("/STACK:8388608")
    endif()
endif()

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
# 0.79.0: JUCE 8.0.4's AudioPluginInstance has a constructor from a C array of channel layouts that cannot compile (no AudioProcessor
# constructor takes that array); Clang 22, the one of Visual Studio 18, checks it even though nothing uses it ("no matching constructor
# for initialization of 'AudioProcessor'"). The downloaded copy loses those two lines, as later JUCE versions did; a copy of JUCE given
# with FETCHCONTENT_SOURCE_DIR_JUCE is left alone.
set(SLS_JUCE_PLUGIN_INSTANCE "${juce_SOURCE_DIR}/modules/juce_audio_processors/processors/juce_AudioPluginInstance.h")
if(EXISTS "${SLS_JUCE_PLUGIN_INSTANCE}")
    file(READ "${SLS_JUCE_PLUGIN_INSTANCE}" SLS_JUCE_TEXT)
    string(REPLACE "\r" "" SLS_JUCE_TEXT "${SLS_JUCE_TEXT}") # (its lines may end in CR LF)
    set(SLS_JUCE_BROKEN "    template <size_t numLayouts>\n    AudioPluginInstance (const short channelLayoutList[numLayouts][2]) : AudioProcessor (channelLayoutList) {}\n")
    string(FIND "${SLS_JUCE_TEXT}" "${SLS_JUCE_BROKEN}" SLS_JUCE_AT)
    if(NOT SLS_JUCE_AT EQUAL -1)
        string(FIND "${juce_SOURCE_DIR}" "${CMAKE_BINARY_DIR}" SLS_JUCE_IN_BUILD)
        if(SLS_JUCE_IN_BUILD EQUAL 0)
            string(REPLACE "${SLS_JUCE_BROKEN}" "" SLS_JUCE_TEXT "${SLS_JUCE_TEXT}")
            file(WRITE "${SLS_JUCE_PLUGIN_INSTANCE}" "${SLS_JUCE_TEXT}")
            message(STATUS "JUCE: removed AudioPluginInstance's array constructor (it does not compile with Clang 22)")
        else()
            message(WARNING "JUCE at ${juce_SOURCE_DIR} has AudioPluginInstance's array constructor, which Clang 22 refuses: "
                            "remove its two lines in ${SLS_JUCE_PLUGIN_INSTANCE} if the build stops there")
        endif()
    endif()
endif()

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
if(WIN32)
    # Windows (0.75.0): libcurl is built in from its pinned source, with Windows's own TLS (Schannel: the system's certificates)
    # and nothing else - HTTP and HTTPS only, no compression or other libraries, a static library.
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    foreach(on BUILD_STATIC_LIBS CURL_USE_SCHANNEL HTTP_ONLY CURL_DISABLE_INSTALL ENABLE_UNICODE CURL_STATIC_CRT)
        set(${on} ON CACHE BOOL "" FORCE)
    endforeach()
    foreach(off BUILD_CURL_EXE BUILD_TESTING BUILD_EXAMPLES BUILD_LIBCURL_DOCS BUILD_MISC_DOCS ENABLE_CURL_MANUAL CURL_USE_OPENSSL
                CURL_USE_LIBPSL USE_LIBIDN2 USE_WIN32_IDN CURL_USE_LIBSSH2 CURL_ZLIB CURL_BROTLI CURL_ZSTD USE_NGHTTP2)
        set(${off} OFF CACHE BOOL "" FORCE)
    endforeach()
    set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        curl
        URL https://github.com/curl/curl/releases/download/curl-8_11_1/curl-8.11.1.tar.xz
        URL_HASH SHA256=c7ca7db48b0909743eaef34250da02c19bc61d4f1dcedd6603f109409536ab56
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        SYSTEM
        ${SLS_JUCE_FETCH_OPTIONS}
    )
    FetchContent_MakeAvailable(curl)
else()
    find_package(CURL REQUIRED)
endif()

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
if(WIN32)
    # Windows (0.75.0): the zip is a resource (RCDATA "SLS_SOUND_LIBRARY") of the program, found with FindResource. An object
    # library, so that the compiled resource goes straight to the linker of each program (a static library's would be dropped).
    configure_file(src/audio/SoundLibraryData.rc.in "${CMAKE_CURRENT_BINARY_DIR}/SoundLibraryData.rc" @ONLY)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/SoundLibraryData.rc" PROPERTIES OBJECT_DEPENDS "${SLS_LIBRARY_ZIP}")
    add_library(SunLoopsLibraryData OBJECT "${CMAKE_CURRENT_BINARY_DIR}/SoundLibraryData.rc" "${SLS_LIBRARY_ZIP}")
else()
    configure_file(src/audio/SoundLibraryData.S.in "${CMAKE_CURRENT_BINARY_DIR}/SoundLibraryData.S" @ONLY)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/SoundLibraryData.S" PROPERTIES OBJECT_DEPENDS "${SLS_LIBRARY_ZIP}")
    add_library(SunLoopsLibraryData STATIC "${CMAKE_CURRENT_BINARY_DIR}/SoundLibraryData.S" "${SLS_LIBRARY_ZIP}")
    set_target_properties(SunLoopsLibraryData PROPERTIES POSITION_INDEPENDENT_CODE ON LINKER_LANGUAGE C)
endif()

# Code shared by the app and the tests (no GUI dependencies).
set(SLS_CORE_SOURCES
    src/audio/AuxBuses.cpp # 0.78.0
    src/audio/DiskPrefetch.cpp # 0.81.0
    src/audio/AsioDrivers.cpp # 0.77.0
    src/audio/Player.cpp
    src/audio/Effects.cpp
    src/audio/Convolver.cpp
    src/audio/Formula.cpp
    src/audio/BuiltinSamples.cpp
    src/audio/SoundLibrary.cpp
    src/audio/DrumSynth.cpp
    src/audio/SpeechSynth.cpp
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
    src/library/DemoProjects.cpp
    src/library/FlpFile.cpp
    src/library/FlpProject.cpp
    src/library/HelpIndex.cpp
    src/library/Sfz.cpp
    src/assistant/HeliosChat.cpp
    src/plugins/PluginScanner.cpp
    src/plugins/PluginHost.cpp
    src/support/Logging.cpp
    src/support/CrashHandler.cpp
    src/support/CrashHandlerWindows.cpp # 0.75.0 (empty elsewhere)
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

# The demo projects (0.65.0): ordinary project files, built into the program (src/library/DemoProjects.cpp lists them).
juce_add_binary_data(SunLoopsDemos HEADER_NAME DemoData.h NAMESPACE DemoData
    SOURCES resources/demos/sunrise-drive.slp resources/demos/white-line.slp resources/demos/ryo.slp)
set_target_properties(SunLoopsDemos PROPERTIES POSITION_INDEPENDENT_CODE ON)
# The program's icon for its own windows: the browser draws it next to project files (0.65.0).
juce_add_binary_data(SunLoopsIcons HEADER_NAME IconData.h NAMESPACE IconData
    SOURCES resources/icons/app_icon_256.png)
set_target_properties(SunLoopsIcons PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- ASIO (Windows, 0.76.0) ---------------------------------------------------------------------------------------------
# The low-latency drivers of most audio interfaces on Windows. JUCE hosts them with Steinberg's ASIO SDK, which is used here under
# its GPLv3 licence (Steinberg offers it so since October 2025; the program is AGPLv3, which may be combined with GPLv3 code). The
# SDK is not copied into the repository: it is downloaded at configure time from Steinberg, pinned and checked by SHA-256.
# -DSLS_ASIO=OFF builds without it (Windows Audio and DirectSound only).
option(SLS_ASIO "Windows: host ASIO drivers (downloads Steinberg's ASIO SDK, used under GPLv3)" ON)
set(SLS_ASIO_ENABLED OFF)
if(WIN32 AND SLS_ASIO)
    FetchContent_Declare(
        asiosdk
        URL https://download.steinberg.net/sdk_downloads/ASIO-SDK_2.3.4_2025-10-15.zip
        URL_HASH SHA256=d5ebf0c20dd2c5f43771fd0c1418f4b361bf52434ee670097cfa6b3a335e2eca
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        ${SLS_JUCE_FETCH_OPTIONS}
    )
    FetchContent_MakeAvailable(asiosdk) # (headers only: nothing to build)
    file(GLOB_RECURSE SLS_ASIO_HEADER "${asiosdk_SOURCE_DIR}/*/iasiodrv.h")
    list(LENGTH SLS_ASIO_HEADER headerCount)
    if(NOT headerCount EQUAL 1)
        message(FATAL_ERROR "The ASIO SDK in ${asiosdk_SOURCE_DIR} has no (or more than one) iasiodrv.h: ${SLS_ASIO_HEADER}")
    endif()
    get_filename_component(SLS_ASIO_INCLUDE_DIR "${SLS_ASIO_HEADER}" DIRECTORY)
    file(GLOB SLS_ASIO_LICENSE "${asiosdk_SOURCE_DIR}/LICENSE*.txt" "${asiosdk_SOURCE_DIR}/license*.txt")
    set(SLS_ASIO_ENABLED ON)
    message(STATUS "ASIO: hosted, with the SDK in ${SLS_ASIO_INCLUDE_DIR}")
endif()
# Lets a program (or a plugin's Standalone) open ASIO devices.
function(sls_enable_asio target)
    if(SLS_ASIO_ENABLED)
        target_include_directories(${target} SYSTEM PRIVATE "${SLS_ASIO_INCLUDE_DIR}")
        target_compile_definitions(${target} PRIVATE JUCE_ASIO=1 SLS_ASIO=1)
    endif()
endfunction()

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
    src/ui/Icons.cpp
    src/ui/PatcherWindow.cpp
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
    src/support/DesktopIntegration.cpp # 0.68.1: the .slp icon and file type, also from the AppImage
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
    SunLoopsDemos
    SunLoopsIcons
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
if(WIN32) # the crash handler's stack traces (0.75.0)
    target_link_libraries(SunLoopsStudio PRIVATE dbghelp)
endif()
sls_enable_asio(SunLoopsStudio)

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
            -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
            "-DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}")
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
# 0.65.0: project files (.slp) show the program's icon in file managers (the MIME type's own icon name, for desktops that do not
# read the <icon> element of resources/linux/sun-loops-studio.xml).
install(FILES resources/icons/app_icon_256.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/mimetypes
        RENAME application-x-sun-loops-studio-project.png)
install(FILES resources/icons/app_icon_16.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/16x16/mimetypes
        RENAME application-x-sun-loops-studio-project.png)
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
    sls_enable_asio(SunBurst) # (its Standalone)
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
    sls_enable_asio(HexForge) # (its Standalone)
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

# --- SunSymphony: the orchestral sampler plugin (0.66.0, Milestone 0), VST3 and Standalone ---------------------------
# The engine (src/orchestra) is plain C++, also built into the tests. The plan: claude/orchestral-sampler-plan.md.
set(ORCHESTRA_SOURCES
    src/orchestra/Instrument.cpp
    src/orchestra/Engine.cpp)
# 0.67.0: the catalog of downloadable instruments (tools/orchestra/catalog.py), built into the plugin.
juce_add_binary_data(SunSymphonyData HEADER_NAME SunSymphonyData.h NAMESPACE SunSymphonyData
    SOURCES resources/orchestra/catalog.json)

option(SLS_BUILD_SUNSYMPHONY "Build the SunSymphony orchestral sampler plugin (VST3 and Standalone)" ON)
if(SLS_BUILD_SUNSYMPHONY)
    juce_add_plugin(SunSymphony
        COMPANY_NAME "Sun Loops Studio"
        PRODUCT_NAME "SunSymphony"
        VERSION ${PROJECT_VERSION}
        PLUGIN_MANUFACTURER_CODE Sunl
        PLUGIN_CODE Ssym
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        FORMATS VST3 Standalone
        VST3_CATEGORIES Instrument Sampler
        COPY_PLUGIN_AFTER_BUILD FALSE)
    target_sources(SunSymphony PRIVATE ${ORCHESTRA_SOURCES} src/library/Sfz.cpp src/support/Http.cpp
                   plugins/sunsymphony/LibraryManager.cpp plugins/sunsymphony/LibraryPanel.cpp
                   plugins/sunsymphony/SunSymphonyProcessor.cpp plugins/sunsymphony/SunSymphonyEditor.cpp plugins/sunsymphony/PluginMain.cpp)
    target_include_directories(SunSymphony PRIVATE src plugins)
    sls_enable_asio(SunSymphony) # (its Standalone)
    target_compile_definitions(SunSymphony PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(SunSymphony PRIVATE
        SunSymphonyData
        CURL::libcurl
        juce::juce_audio_utils
        juce::juce_cryptography
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    install(DIRECTORY "${CMAKE_BINARY_DIR}/SunSymphony_artefacts/$<CONFIG>/VST3/SunSymphony.vst3"
            DESTINATION lib/vst3
            USE_SOURCE_PERMISSIONS)
    add_dependencies(SunLoopsStudio SunSymphony_VST3)
endif()

# --- SunRiff: the electric guitar plugin (0.68.0), after FL Slayer, VST3 and Standalone ---------------------------------
set(SUNRIFF_SOURCES
    src/sunriff/RiffString.cpp
    src/sunriff/RiffAmp.cpp
    src/sunriff/RiffFx.cpp
    src/sunriff/RiffPlayer.cpp
    src/sunriff/RiffParams.cpp
    src/sunriff/RiffPresets.cpp
    src/sunriff/RiffProcessor.cpp
    src/synthkit/SynthProcessor.cpp)

option(SLS_BUILD_SUNRIFF "Build the SunRiff guitar plugin (VST3 and Standalone)" ON)
if(SLS_BUILD_SUNRIFF)
    juce_add_plugin(SunRiff
        COMPANY_NAME "Sun Loops Studio"
        PRODUCT_NAME "SunRiff"
        VERSION ${PROJECT_VERSION}
        PLUGIN_MANUFACTURER_CODE Sunl
        PLUGIN_CODE Srif
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        FORMATS VST3 Standalone
        VST3_CATEGORIES Instrument Synth
        COPY_PLUGIN_AFTER_BUILD FALSE)
    target_sources(SunRiff PRIVATE ${SUNRIFF_SOURCES} plugins/common/HardwareUi.cpp plugins/common/SynthUi.cpp
                   plugins/sunriff/RiffEditor.cpp plugins/sunriff/PluginMain.cpp)
    target_include_directories(SunRiff PRIVATE src plugins)
    sls_enable_asio(SunRiff) # (its Standalone)
    target_compile_definitions(SunRiff PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(SunRiff PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    install(DIRECTORY "${CMAKE_BINARY_DIR}/SunRiff_artefacts/$<CONFIG>/VST3/SunRiff.vst3"
            DESTINATION lib/vst3
            USE_SOURCE_PERMISSIONS)
    add_dependencies(SunLoopsStudio SunRiff_VST3)
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
    tests/test_orchestra.cpp
    tests/test_sunsymphony_library.cpp
    tests/test_sunriff.cpp
    src/support/DesktopIntegration.cpp
    plugins/sunsymphony/LibraryManager.cpp
    src/ui/PianoRoll.cpp # 0.58.0: the spare quarter is tested through its public functions
    ${SLS_CORE_SOURCES}
    ${SUNBURST_SOURCES}
    ${HEXFORGE_SOURCES}
    ${ORCHESTRA_SOURCES}
    ${SUNRIFF_SOURCES})

target_include_directories(SunLoopsStudioTests PRIVATE src plugins)
target_include_directories(SunLoopsStudioTests SYSTEM PRIVATE third_party/signalsmith third_party/tinysoundfont)
target_compile_definitions(SunLoopsStudioTests PRIVATE ${SLS_COMMON_DEFINITIONS} SLS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(SunLoopsStudioTests PRIVATE "LINKER:--export-dynamic-symbol=_ZN3sls*")
endif()
if(WIN32) # the crash handler's stack traces and the tests' little HTTP server (0.75.0)
    target_link_libraries(SunLoopsStudioTests PRIVATE dbghelp ws2_32)
endif()
if(SLS_ASIO_ENABLED) # 0.76.0: the ASIO device type is checked
    target_link_libraries(SunLoopsStudioTests PRIVATE juce::juce_audio_devices)
    sls_enable_asio(SunLoopsStudioTests)
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
    SunLoopsDemos
    SunSymphonyData
    SunLoopsIcons
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_cryptography
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
if(SLS_BUILD_SUNSYMPHONY)
    add_dependencies(SunLoopsStudioTests SunSymphony_VST3)
endif()
if(SLS_BUILD_SUNRIFF)
    add_dependencies(SunLoopsStudioTests SunRiff_VST3)
endif()

enable_testing()
add_test(NAME core COMMAND SunLoopsStudioTests)

# The demo projects' helper (0.65.0, tools/demos): renders a project file headless and lists what a demo can use. Only for making the
# demos (-DSLS_BUILD_DEMO_TOOL=ON); the program does not need it.
option(SLS_BUILD_DEMO_TOOL "Build tools/demos/DemoTool (renders .slp files headless, for making the demo projects)" OFF)
if(SLS_BUILD_DEMO_TOOL)
    juce_add_console_app(SunLoopsDemoTool PRODUCT_NAME "SunLoopsDemoTool")
    target_sources(SunLoopsDemoTool PRIVATE tools/demos/DemoTool.cpp ${SLS_CORE_SOURCES})
    target_include_directories(SunLoopsDemoTool PRIVATE src)
    target_include_directories(SunLoopsDemoTool SYSTEM PRIVATE third_party/signalsmith third_party/tinysoundfont)
    target_compile_definitions(SunLoopsDemoTool PRIVATE ${SLS_COMMON_DEFINITIONS})
    target_link_libraries(SunLoopsDemoTool PRIVATE
        spdlog::spdlog nlohmann_json::nlohmann_json CURL::libcurl SunLoopsSounds SunLoopsLibraryData SunLoopsDemos
        juce::juce_audio_formats juce::juce_audio_processors juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_dependencies(SunLoopsDemoTool SunBurst_VST3 HexForge_VST3)
endif()
