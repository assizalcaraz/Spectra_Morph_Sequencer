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
#include "../dsp/phase/PhaseManager.h"
#include "../dsp/scramble/SpectralFrameStore.h"
#include "../dsp/scramble/TemporalScrambler.h"
#include "../simulation/ParticleSimulator.h"
#include "FileSourceManager.h"

#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>

namespace ParamID {
    inline constexpr const char* ProcessMode       = "process_mode";
    inline constexpr const char* CoherenceChaos    = "coherence_chaos";
    inline constexpr const char* Density           = "density";
    inline constexpr const char* TonalResidual       = "tonal_residual";
    inline constexpr const char* Gravity             = "gravity";
    inline constexpr const char* Motion              = "motion";
    inline constexpr const char* Decay               = "decay";
    inline constexpr const char* Spread              = "spread";
    inline constexpr const char* DryWet              = "dry_wet";
    inline constexpr const char* MaxPartials         = "max_partials";
    inline constexpr const char* BirthThreshold      = "birth_threshold";
    inline constexpr const char* SegmentStart        = "segment_start";
    inline constexpr const char* SegmentEnd          = "segment_end";
    inline constexpr const char* TemporalFragmentMs  = "temporal_fragment_ms";
    inline constexpr const char* BinScatter          = "bin_scatter";
    inline constexpr const char* RandomSeed          = "random_seed";
    inline constexpr const char* SpectralQuality     = "spectral_quality";
    inline constexpr const char* ExportNormalize     = "export_normalize";
}

class SpectraMorphAudioProcessor final : public juce::AudioProcessor {
public:
    SpectraMorphAudioProcessor();
    ~SpectraMorphAudioProcessor() override;

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

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    const ParticleSnapshot* read_snapshot() const { return snapshots_.read(); }
    bool read_visual(VisualState& vs) { return visual_queue_.read(vs); }

    Scheduler& get_scheduler() { return scheduler_; }
    juce::AudioProcessorValueTreeState& get_apvts() { return apvts_; }

    float telemetry_f0() const { return telemetry_f0_.load(); }
    float telemetry_flux() const { return telemetry_flux_.load(); }
    float telemetry_transient() const { return telemetry_transient_.load(); }
    uint32_t telemetry_scramble_read() const {
        return telemetry_scramble_read_.load();
    }
    uint32_t telemetry_frame_store() const {
        return frame_store_.num_frames();
    }

    ProcessMode current_process_mode() const;
    bool isFileGranularMode() const;

    FileSourceManager& file_source() { return file_source_; }
    const FileSourceManager& file_source() const { return file_source_; }

    bool loadSourceFile(const juce::File& file, juce::String& error);
    void setFilePlaying(bool play);
    bool isFilePlaying() const { return file_playing_.load(); }
    bool renderSegmentToFile(const juce::File& dest, juce::String& error);

private:
    std::thread dsp_thread_;
    std::thread sim_thread_;
    std::atomic<bool> running_{false};

    void dsp_thread_func();
    void sim_thread_func();
    void syncParamsFromApvts();
    void merge_sim_into_pool();
    void wait_for_sim_frame(uint32_t frame);
    TransientMode transient_mode_for_coherence(float coherence) const;

    void applyFftConfig();
    bool shouldRunSim() const;
    void syncFileSegmentFromApvts();
    uint32_t framesPerFragment() const;
    uint64_t scrambleSeed() const;

    FFTProcessor      fft_;
    PartialTracker    tracker_;
    Resynthesizer     resynth_;
    ParticleSimulator simulator_;

    PartialPool       pool_;
    AdditiveBuffer    add_buf_;
    DoubleBuffer<ParticleSnapshot> snapshots_;
    DoubleBuffer<ParticleSnapshot> sim_snapshots_;

    AudioRingBuffer   input_ring_;
    AudioRingBuffer   output_ring_;
    VisualRingBuffer  visual_queue_;

    SpectralFrameStore frame_store_;
    FileSourceManager  file_source_;

    std::vector<float> work_mag_;
    std::vector<float> work_phase_;
    std::vector<float> work_raw_phase_;

    Scheduler         scheduler_;
    uint32_t          frame_counter_ = 0;
    double            sample_rate_   = 48000.0;
    int               block_size_    = 256;
    uint32_t          hop_size_      = 512;
    uint32_t          fft_size_      = 2048;
    uint32_t          dry_latency_samples_ = 1536;
    float             tracked_f0_    = 0.0f;
    uint64_t          phase_seed_    = 12345;
    uint64_t          sim_rng_seed_  = 54321;

    std::mutex              sim_mutex_;
    std::condition_variable sim_cv_;
    uint32_t                dsp_ready_frame_ = 0;
    std::atomic<uint32_t>   sim_done_frame_{0};

    std::atomic<float> telemetry_f0_{0.0f};
    std::atomic<float> telemetry_flux_{0.0f};
    std::atomic<float> telemetry_transient_{0.0f};
    std::atomic<uint32_t> telemetry_scramble_read_{0};

    std::atomic<bool> file_playing_{false};
    std::atomic<bool> file_exporting_{false};
    std::atomic<bool> non_realtime_render_{false};

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>
        dry_delay_line_;

    std::atomic<float> coherence_chaos_{0.2f};
    std::atomic<float> density_{0.5f};
    std::atomic<float> tonal_residual_{0.7f};
    std::atomic<float> gravity_{1.0f};
    std::atomic<float> motion_{0.3f};
    std::atomic<float> decay_{0.5f};
    std::atomic<float> spread_{0.5f};
    std::atomic<float> dry_wet_{0.5f};
    std::atomic<float> birth_threshold_{0.1f};
    std::atomic<float> max_partials_{0.5f};
    std::atomic<float> segment_start_{0.0f};
    std::atomic<float> segment_end_{1.0f};
    std::atomic<float> temporal_fragment_ms_{80.0f};
    std::atomic<float> bin_scatter_{0.0f};
    std::atomic<float> random_seed_{42.0f};
    std::atomic<float> spectral_quality_{0.0f};
    std::atomic<float> export_normalize_{1.0f};

    juce::AudioProcessorValueTreeState apvts_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectraMorphAudioProcessor)
};
