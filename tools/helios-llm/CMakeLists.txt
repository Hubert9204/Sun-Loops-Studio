# sun-loops-helios (0.64.0): the helper process that runs Helios's language model (llama.cpp). A project of its own because it is
# built once per processor: the main CMakeLists.txt builds it with ExternalProject for each architecture of the app (on a Mac:
# arm64 and x86_64, then joined with lipo), since llama.cpp cannot be built for two processors in one configuration.
#
# By hand (for trying it): cmake -S tools/helios-llm -B build-helios -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build-helios
cmake_minimum_required(VERSION 3.22)
set(SLS_VERSION "0.64.0" CACHE STRING "The program's version (the main build passes its own)")
project(SunLoopsHelios VERSION ${SLS_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SLS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "The Sun Loops Studio source folder (for src/assistant)")

# The processor this copy is for.
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    set(SLS_HELIOS_ARCH "${CMAKE_OSX_ARCHITECTURES}")
else()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" SLS_HELIOS_ARCH)
endif()
list(LENGTH SLS_HELIOS_ARCH archCount)
if(archCount GREATER 1)
    message(FATAL_ERROR "sun-loops-helios is built for one processor at a time (got ${SLS_HELIOS_ARCH})")
endif()
message(STATUS "sun-loops-helios: building for ${SLS_HELIOS_ARCH}")

# --- llama.cpp, pinned ------------------------------------------------------------------------------------------------------
# Only the library: no tools, server, tests, network or OpenMP (ggml's own thread pool), linked in statically.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
foreach(off LLAMA_BUILD_COMMON LLAMA_BUILD_TESTS LLAMA_BUILD_TOOLS LLAMA_BUILD_EXAMPLES LLAMA_BUILD_SERVER LLAMA_BUILD_APP
            LLAMA_BUILD_UI LLAMA_OPENSSL LLAMA_ALL_WARNINGS GGML_NATIVE GGML_OPENMP GGML_BLAS GGML_BACKEND_DL)
    set(${off} OFF CACHE BOOL "" FORCE)
endforeach()

# Processor features are fixed (not "this machine's"), so that a copy built anywhere runs on the oldest machine it is for.
if(SLS_HELIOS_ARCH MATCHES "x86_64|amd64|x64")
    set(GGML_SSE42 ON CACHE BOOL "" FORCE)
    set(GGML_AVX ON CACHE BOOL "" FORCE)
    set(GGML_F16C ON CACHE BOOL "" FORCE)
    if(APPLE)
        # Intel Macs from 2012 (Ivy Bridge, macOS 10.15): AVX and F16C, no AVX2 / FMA. No Metal (their GPUs are too old for it)
        # and no Accelerate (llama.cpp asks for the macOS 13.3 one).
        set(simd OFF)
        set(GGML_METAL OFF CACHE BOOL "" FORCE)
        set(GGML_ACCELERATE OFF CACHE BOOL "" FORCE)
    else()
        set(simd ON) # Linux and Windows: processors since 2013 (Haswell, AMD since 2015)
    endif()
    set(GGML_AVX2 ${simd} CACHE BOOL "" FORCE)
    set(GGML_FMA ${simd} CACHE BOOL "" FORCE)
    set(GGML_BMI2 ${simd} CACHE BOOL "" FORCE)
elseif(APPLE AND SLS_HELIOS_ARCH STREQUAL "arm64")
    # Apple silicon: the model runs on the GPU (Metal), its shaders built into the program.
    set(GGML_METAL ON CACHE BOOL "" FORCE)
    set(GGML_METAL_EMBED_LIBRARY ON CACHE BOOL "" FORCE)
endif()

include(FetchContent)
FetchContent_Declare(
    llama
    GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
    GIT_TAG b12377 # (14 November 2026: Gemma 4)
    GIT_SHALLOW ON
    SYSTEM)
FetchContent_MakeAvailable(llama)

# --- JUCE (juce_core and juce_events only: the pipe to the program) -------------------------------------------------------
# The main build passes its own copy (FETCHCONTENT_SOURCE_DIR_JUCE), so JUCE is not downloaded again.
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_console_app(SunLoopsHelios PRODUCT_NAME "sun-loops-helios")
target_sources(SunLoopsHelios PRIVATE HeliosWorker.cpp)
target_include_directories(SunLoopsHelios PRIVATE "${SLS_SOURCE_DIR}/src")
target_compile_definitions(SunLoopsHelios PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(SunLoopsHelios PRIVATE
    llama
    juce::juce_core
    juce::juce_events
    juce::juce_recommended_config_flags)

# The helper's file name everywhere: sun-loops-helios (sun-loops-helios.exe on Windows).
set_target_properties(SunLoopsHelios PROPERTIES OUTPUT_NAME "sun-loops-helios")
install(PROGRAMS "$<TARGET_FILE:SunLoopsHelios>" DESTINATION bin)
