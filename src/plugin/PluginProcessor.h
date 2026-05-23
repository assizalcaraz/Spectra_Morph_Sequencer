#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "../core/types/Partial.h"
#include "../core/types/Peak.h"
#include "../core/types/Snapshot.h"
#include "../core/types/ExternalField.h"
#include "../core/memory/PartialPool.h"
#include "../core/memory/RingBuffer.h"
#include "../core/memory/AdditiveBuffer.h"
#include "../core/scheduler/Scheduler.h"
#include "../core/threading/ThreadUtils.h"
#include "../dsp/fft/FFTProcessor.h"
#include "../dsp/tracking/PartialTracker.h"
#include "../dsp/resynthesis/Resynthesizer.h"

#include <atomic>
#include <thread>

// ─── Parameter IDs ───────────────────────────────────────────────────
namespace ParamID {
    inline constexpr const char* CoherenceChaos  = "coherence_chaos";
    inline constexpr const char* Density         = "density";
    inline constexpr const char* TonalResidual   = "tonal_residual";
    inline constexpr const char* Gravity         = "gravity";
    inline constexpr const char* Motion          = "motion";
    inline constexpr const char* Decay           = "decay";
    inline constexpr const char* Spread          = "spread";
    inline constexpr const char* DryWet          = "dry_wet";
    inline constexpr const char* MaxPartials     = "max_partials";
    inline constexpr const char* BirthThreshold  = "birth_threshold";
    inline constexpr const char* PhaseMode       = "phase_mode";
    inline constexpr const char* QualityMode     = "quality_mode";
}

class SpectraMorphAudioProcessor final : public juce::AudioProcessor {
public:
    SpectraMorphAudioProcessor();
    ~SpectraMorphAudioProcessor() override;

    // ── JUCE overrides ─────────────────────────────────────────────
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override { return "SpectraMorph"; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ── Thread-safe parameter access ───────────────────────────────
    float getParam(std::atomic<float>& param) const { return param.load(); }
    void  setParam(std::atomic<float>& param, float v) { param.store(v); }

    // ── Snapshot access ────────────────────────────────────────────
    const ParticleSnapshot* read_snapshot() const { return snapshots_.read(); }
    bool read_visual(VisualState& vs) { return visual_queue_.read(vs); }

    // ── Scheduler ──────────────────────────────────────────────────
    Scheduler& get_scheduler() { return scheduler_; }

    // ── Parameter access (for editor attachments) ──────────────────
    juce::AudioProcessorValueTreeState& get_apvts() { return apvts_; }

private:
    // ── Threads ────────────────────────────────────────────────────
    std::thread dsp_thread_;
    std::thread sim_thread_;
    std::atomic<bool> running_{false};

    void dsp_thread_func();
    void sim_thread_func();
    void syncParamsFromApvts();
    void applyParticleEffects();

    // ── DSP pipeline ───────────────────────────────────────────────
    FFTProcessor      fft_;
    PartialTracker    tracker_;
    Resynthesizer     resynth_;

    // ── Memory ─────────────────────────────────────────────────────
    PartialPool       pool_;
    AdditiveBuffer    add_buf_;
    DoubleBuffer<ParticleSnapshot> snapshots_;

    // ── Communication ──────────────────────────────────────────────
    AudioRingBuffer   input_ring_;
    AudioRingBuffer   output_ring_;
    VisualRingBuffer  visual_queue_;

    // ── Scheduling ─────────────────────────────────────────────────
    Scheduler         scheduler_;
    uint32_t          frame_counter_ = 0;
    double            sample_rate_   = 48000.0;
    int               block_size_    = 256;
    uint32_t          hop_size_      = 512;
    uint32_t          fft_size_      = 2048;

    // ── Parameters (atomic cache, SPECS_12 §4) ─────────────────────
    std::atomic<float> coherence_chaos_{0.2f};
    std::atomic<float> density_{0.5f};
    std::atomic<float> tonal_residual_{0.7f};
    std::atomic<float> gravity_{1.0f};
    std::atomic<float> motion_{0.3f};
    std::atomic<float> decay_{0.5f};
    std::atomic<float> spread_{0.5f};
    std::atomic<float> dry_wet_{0.5f};
    std::atomic<float> birth_threshold_{0.1f};
    std::atomic<float> max_partials_{0.5f};  // 0-1 mapped to 16-256

    // ── APVTS ──────────────────────────────────────────────────────
    juce::AudioProcessorValueTreeState apvts_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectraMorphAudioProcessor)
};
