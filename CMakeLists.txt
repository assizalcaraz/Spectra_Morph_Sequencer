cmake_minimum_required(VERSION 3.22)

project(SpectraMorph
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "Spectral Particle Instrument"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0)

# ── JUCE ──────────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG master
)
FetchContent_MakeAvailable(JUCE)

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
    src/dsp/resynthesis/Resynthesizer.h
    src/dsp/resynthesis/ResidualSynth.h
)

set(SIMULATION_SOURCES
)

set(PLUGIN_SOURCES
    src/plugin/PluginProcessor.h
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginEditor.h
    src/plugin/PluginEditor.cpp
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

    FORMATS VST3 AU

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
)

target_include_directories(SpectraMorph
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ── Test target ───────────────────────────────────────────────────────
add_executable(SpectraMorph_tests
    tests/TestRunner.cpp
    tests/test_memory.cpp
    tests/test_scheduler.cpp
    tests/test_resynthesis.cpp
)

target_include_directories(SpectraMorph_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(SpectraMorph_tests
    PRIVATE
        juce::juce_dsp
)

# ── Compiler flags ────────────────────────────────────────────────────
if(APPLE)
    target_compile_options(SpectraMorph PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-deprecated-declarations
        -ffast-math
        -march=native
    )
endif()
