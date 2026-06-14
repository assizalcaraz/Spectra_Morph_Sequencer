cmake_minimum_required(VERSION 3.22)

project(SpectraMorph
    VERSION 0.2.0
    LANGUAGES CXX
    DESCRIPTION "Spectral Particle Instrument"
)

set(SM_BUILD_TAG "granular-file-r6")

find_package(Git QUIET)
set(SM_GIT_SHA "nogit")
if(Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE SM_GIT_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/BuildVersion.h.in
    ${CMAKE_BINARY_DIR}/generated/BuildVersion.h
    @ONLY
)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0)

# Standalone: abrir UI sin Reaper (lldb / Xcode). Desactivar: -DSM_ENABLE_STANDALONE=OFF
option(SM_ENABLE_STANDALONE "Build Standalone app for offline debugging" ON)
if(SM_ENABLE_STANDALONE)
    set(SM_PLUGIN_FORMATS VST3 AU Standalone)
else()
    set(SM_PLUGIN_FORMATS VST3 AU)
endif()

# ── JUCE ──────────────────────────────────────────────────────────────
# Local install: cmake -DJUCE_PATH=/ruta/a/JUCE ...
if(JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
    add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/juce-build EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    set(FETCHCONTENT_UPDATES_DISCONNECTED JUCE ON CACHE BOOL "" FORCE)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG master
        UPDATE_DISCONNECTED 1
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# ── Source groups ─────────────────────────────────────────────────────
set(CORE_SOURCES
    src/core/types/Types.h
    src/core/types/Partial.h
    src/core/types/Peak.h
    src/core/types/Snapshot.h
    src/core/types/ExternalField.h
    src/core/threading/ThreadUtils.h
    src/core/scheduler/Scheduler.h
    src/core/memory/RingBuffer.h
    src/core/memory/PartialPool.h
    src/core/memory/AdditiveBuffer.h
)

set(DSP_SOURCES
    src/dsp/fft/FFTProcessor.h
    src/dsp/tracking/PartialTracker.h
    src/dsp/tracking/PeakUtils.h
    src/dsp/phase/PhaseManager.h
    src/dsp/resynthesis/Resynthesizer.h
    src/dsp/resynthesis/ResidualSynth.h
    src/dsp/debug/SpectralMetrics.h
    src/dsp/scramble/SpectralFrameStore.h
    src/dsp/scramble/TemporalScrambler.h
)

set(SIMULATION_SOURCES
    src/simulation/ParticleSimulator.h
)

set(PLUGIN_SOURCES
    src/plugin/PluginProcessor.h
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginEditor.h
    src/plugin/PluginEditor.cpp
    src/plugin/FileSourceManager.h
)

set(UI_SOURCES
)

# ── Plugin target ─────────────────────────────────────────────────────
juce_add_plugin(SpectraMorph
    PRODUCT_NAME "SpectraMorph"
    COMPANY_NAME "spectramorph"
    COMPANY_WEBSITE ""
    COMPANY_EMAIL ""
    COMPANY_ADDRESS ""
    VERSION "${PROJECT_VERSION}"
    DESCRIPTION "${PROJECT_DESCRIPTION}"

    FORMATS ${SM_PLUGIN_FORMATS}

    VST2_COPY_AUTOMATION OFF

    NEEDS_MIDI_INPUT ON
    NEEDS_MIDI_OUTPUT OFF
    IS_SYNTH OFF
    EDITOR_PLUGIN_TYPE Generic

    COPY_PLUGIN_AFTER_BUILD ON
    PLUGIN_MANUFACTURER_CODE SpMp
    PLUGIN_CODE SpMp
)

target_sources(SpectraMorph
    PRIVATE
        ${CORE_SOURCES}
        ${DSP_SOURCES}
        ${SIMULATION_SOURCES}
        ${PLUGIN_SOURCES}
        ${UI_SOURCES}
)

# Silence JUCE VST2/VST3 param ID warning — we only build VST3 + AU
add_compile_definitions(
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(SpectraMorph
    PRIVATE
        juce::juce_dsp
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_formats
)

target_include_directories(SpectraMorph
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated
)

# ── Test target ───────────────────────────────────────────────────────
add_executable(SpectraMorph_tests
    tests/TestRunner.cpp
    tests/test_memory.cpp
    tests/test_scheduler.cpp
    tests/test_resynthesis.cpp
    tests/test_phase2_validation.cpp
    tests/test_fidelity.cpp
    tests/test_triangle_resynth.cpp
    tests/test_scramble.cpp
    tests/test_file_segment.cpp
    tests/test_window_pattern.cpp
    tests/test_tempo_utils.cpp
    tests/test_pitch_spread.cpp
    tests/test_file_granular_fidelity.cpp
)

target_include_directories(SpectraMorph_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(SpectraMorph_tests
    PRIVATE
        juce::juce_dsp
)

target_compile_options(SpectraMorph_tests PRIVATE -UNDEBUG)

# ── Compiler flags ────────────────────────────────────────────────────
if(APPLE)
    target_compile_options(SpectraMorph PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-deprecated-declarations
        -ffast-math
        -march=native
    )
endif()
