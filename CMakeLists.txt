# =============================================================================
# FL Studio Clone DAW — CMake Build System
# Target: Nobara Linux 44 (Fedora-based, PipeWire, KDE Plasma)
# Language: C++20 with JUCE 8
# =============================================================================

cmake_minimum_required(VERSION 3.22)
project(FLStudioClone VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Opzioni di build ---
option(ENABLE_JACK "Enable JACK audio backend support" OFF)
option(ENABLE_PIPEWIRE "Enable PipeWire native backend" ON)
option(DEBUG_AUDIO_TIMING "Log timing info in audio callback (dev only)" OFF)

# --- Dipendenze sistema ---
find_package(PkgConfig REQUIRED)

# JUCE: usa la copia locale (clonata da GitHub)
set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/juce-src" CACHE PATH "Path to JUCE source")
add_subdirectory(${JUCE_DIR} build/juce-build EXCLUDE_FROM_ALL)

# PipeWire (opzionale, per info di sistema)
pkg_check_modules(PW libpipewire-0.3>=1.0 IMPORTED_TARGET)
if(PW_FOUND)
    message(STATUS "PipeWire found: ${PW_VERSION}")
else()
    message(WARNING "PipeWire dev libraries not found — JUCE backend still works")
endif()

# JACK (opzionale)
pkg_check_modules(JACK jack IMPORTED_TARGET)
if(JACK_FOUND AND ENABLE_JACK)
    message(STATUS "JACK found, enabling JACK backend")
else()
    message(STATUS "JACK not found or disabled — using PipeWire/Pulse backend")
endif()

# --- Sorgenti del progetto ---
set(PROJECT_SOURCES
    src/main.cpp
    src/audio/AudioEngine.h src/audio/AudioEngine.cpp
    src/plugins/PluginHost.h src/plugins/PluginHost.cpp
    src/plugins/PluginProcessor.h src/plugins/PluginProcessor.cpp
    src/plugins/PluginScanner.h src/plugins/PluginScanner.cpp
    src/audio/Oscillator.h src/audio/Oscillator.cpp
    src/audio/RingBuffer.h
    src/ui/MainWindow.h src/ui/MainWindow.cpp
    src/ui/AudioMeter.h src/ui/AudioMeter.cpp
    src/common/AtomicFloat.h
    src/common/Constants.h
)

# --- Creazione eseguibile (app desktop) ---
juce_add_gui_app(${PROJECT_NAME}
    VERSION ${PROJECT_VERSION}
    ICON_BIG   resources/icons/app_icon_256.png
    ICON_SMALL resources/icons/app_icon_16.png
    MICROPHONE_PERMISSION_ENABLED  FALSE
    CAMERA_PERMISSION_ENABLED      FALSE
)

# Aggiungi i nostri sorgenti
juce_generate_juce_header(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCES})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link con JUCE modules
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        juce::juce_audio_utils
        juce::juce_gui_extra
        juce::juce_gui_basics
        juce::juce_dsp
        juce::juce_audio_processors  # Aggiunto per VST3 supporto

        # PipeWire (se disponibile)
        ${PW_LINK_LIBRARIES}

        # JACK (se abilitato e disponibile)
        ${JACK_LINK_LIBRARIES}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Define per backend audio
if(ENABLE_PIPEWIRE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_PIPEWIRE_BACKEND=1)
endif()

if(ENABLE_JACK)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_JACK_BACKEND=1)
endif()

if(DEBUG_AUDIO_TIMING)
    target_compile_definitions(${PROJECT_NAME} PRIVATE DEBUG_AUDIO_TIMING=1)
endif()

# Installazione
install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)