#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
    constexpr double MIN_FREQ = 20.0;

    float freq_to_spectral_pos(double freq_hz) {
        return static_cast<float>(std::log2(freq_hz / MIN_FREQ));
    }
}

static juce::AudioProcessorValueTreeState::ParameterLayout create_params() {
    juce::AudioProcessorValueTreeState::ParameterLayout params;

    auto add_float = [&](const char* id, const char* name,
                          float min, float max, float def) {
        params.add(std::make_unique<juce::AudioParameterFloat>(
            id, name,
            juce::NormalisableRange<float>(min, max, 0.01f), def));
    };

    add_float(ParamID::CoherenceChaos, "Coherence ↔ Chaos", 0.0f, 1.0f, 0.2f);
    add_float(ParamID::Density,        "Density",            0.0f, 1.0f, 0.5f);
    add_float(ParamID::TonalResidual,  "Tonal / Residual",   0.0f, 1.0f, 0.7f);
    add_float(ParamID::Gravity,        "Gravity",            0.0f, 10.0f, 1.0f);
    add_float(ParamID::Motion,         "Motion",             0.0f, 1.0f, 0.3f);
    add_float(ParamID::Decay,          "Decay",              0.0f, 1.0f, 0.5f);
    add_float(ParamID::Spread,         "Spread",             0.0f, 1.0f, 0.5f);
    add_float(ParamID::DryWet,         "Dry / Wet",          0.0f, 1.0f, 0.5f);
    add_float(ParamID::BirthThreshold, "Birth Threshold",    0.0f, 1.0f, 0.1f);
    add_float(ParamID::MaxPartials,    "Max Partials",       0.0f, 1.0f, 0.5f);

    return params;
}

// ── Processor ────────────────────────────────────────────────────────
SpectraMorphAudioProcessor::SpectraMorphAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts_(*this, nullptr, "Parameters", create_params())
{
}

SpectraMorphAudioProcessor::~SpectraMorphAudioProcessor() {
    releaseResources();
}

void SpectraMorphAudioProcessor::prepareToPlay(double sampleRate, int blockSize) {
    // Ensure any previous session is torn down before re-initializing
    releaseResources();

    sample_rate_ = sampleRate;
    block_size_  = static_cast<uint32_t>(blockSize);
    fft_size_    = 2048;
    hop_size_    = fft_size_ / 4;

    // Init pipeline
    fft_.prepare(fft_size_, hop_size_, static_cast<float>(sample_rate_));
    tracker_.prepare(static_cast<float>(sample_rate_), fft_size_, hop_size_);
    resynth_.prepare(static_cast<float>(sample_rate_));

    // Clear state
    pool_.clear();
    add_buf_.clear();
    snapshots_.reset();
    input_ring_.clear();
    output_ring_.clear();
    visual_queue_.clear();
    scheduler_.reset();
    frame_counter_ = 0;

    // Copy initial params
    coherence_chaos_.store(apvts_.getRawParameterValue(ParamID::CoherenceChaos)->load());
    density_.store(apvts_.getRawParameterValue(ParamID::Density)->load());
    tonal_residual_.store(apvts_.getRawParameterValue(ParamID::TonalResidual)->load());
    gravity_.store(apvts_.getRawParameterValue(ParamID::Gravity)->load());
    motion_.store(apvts_.getRawParameterValue(ParamID::Motion)->load());
    decay_.store(apvts_.getRawParameterValue(ParamID::Decay)->load());
    spread_.store(apvts_.getRawParameterValue(ParamID::Spread)->load());
    dry_wet_.store(apvts_.getRawParameterValue(ParamID::DryWet)->load());
    birth_threshold_.store(apvts_.getRawParameterValue(ParamID::BirthThreshold)->load());
    max_partials_.store(apvts_.getRawParameterValue(ParamID::MaxPartials)->load());

    // Start worker threads
    running_ = true;
    dsp_thread_ = std::thread([this] { dsp_thread_func(); });
    sim_thread_ = std::thread([this] { sim_thread_func(); });
}

void SpectraMorphAudioProcessor::releaseResources() {
    running_ = false;
    if (dsp_thread_.joinable()) dsp_thread_.join();
    if (sim_thread_.joinable()) sim_thread_.join();
}

void SpectraMorphAudioProcessor::processBlock(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    int ns = buffer.getNumSamples();
    int nc = buffer.getNumChannels();
    float mix = dry_wet_.load();

    // Sum input to mono (safe: no allocation in audio thread)
    for (int i = 0; i < ns; ++i) {
        float s = 0.0f;
        for (int ch = 0; ch < nc; ++ch)
            s += buffer.getReadPointer(ch)[i];
        input_ring_.write(s / static_cast<float>(nc));
    }

    // Read mono wet output once per sample, apply to all channels
    for (int i = 0; i < ns; ++i) {
        float wet = 0.0f;
        output_ring_.read(wet);
        for (int ch = 0; ch < nc; ++ch) {
            auto* out = buffer.getWritePointer(ch);
            auto* in  = buffer.getReadPointer(ch);
            out[i] = in[i] * (1.0f - mix) + wet * mix;
        }
    }
}

// ── DSP Worker Thread ───────────────────────────────────────────────
void SpectraMorphAudioProcessor::dsp_thread_func() {
    // Local buffer for FFT input block
    std::vector<float> input_block(hop_size_, 0.0f);

    while (running_) {
        scheduler_.begin_frame();

        // Read hop_size samples from ring buffer
        bool got_data = true;
        for (uint32_t i = 0; i < hop_size_; ++i) {
            if (!input_ring_.read(input_block[i])) {
                got_data = false;
                break;
            }
        }

        if (!got_data) {
            scheduler_.end_frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        // 1. FFT
        fft_.process(input_block.data());

        // 2. Peak detection
        Peak peaks[MAX_PEAKS];
        uint32_t num_peaks = 0;
        uint32_t half_n = fft_.half_n();
        float threshold = fft_.noise_floor() * birth_threshold_.load() + 0.001f;

        const float* mag   = fft_.magnitude();
        const float* phase = fft_.phase();

        for (uint32_t i = 1; i < half_n - 1 && num_peaks < MAX_PEAKS; ++i) {
            if (mag[i] > mag[i - 1] && mag[i] > mag[i + 1] && mag[i] > threshold) {
                auto& p = peaks[num_peaks++];
                p.bin_index = static_cast<uint16_t>(i);
                p.frequency = fft_.bin_freq(i);
                p.magnitude = mag[i];
                p.phase     = phase[i];
            }
        }

        // 3. Update params from atomic cache
        float density_val = density_.load();
        birth_threshold_.store(
            apvts_.getRawParameterValue(ParamID::BirthThreshold)->load());

        // 4. Partial tracking (cadenced)
        ++frame_counter_;
        if (scheduler_.tick(frame_counter_)) {
            tracker_.track(peaks, num_peaks, pool_, frame_counter_);
            scheduler_.enforce_budget(pool_);
        }

        // 5. Write snapshot
        {
            auto* snap = snapshots_.write_buffer();
            pool_.write_snapshot(*snap, frame_counter_);
            snapshots_.commit();
        }

        // 6. Resynthesis
        const auto* snap = snapshots_.read();
        if (snap && snap->num_partials > 0) {
            resynth_.render(*snap, static_cast<float>(sample_rate_), hop_size_);
            const float* out = resynth_.output_buffer();
            for (uint32_t i = 0; i < hop_size_; ++i)
                output_ring_.write(out[i]);
        }

        // 7. Visual state
        {
            VisualState vs{};
            const auto* s = snapshots_.read();
            if (s) {
                vs.frame_number      = s->frame_number;
                vs.num_partials      = s->num_partials;
                vs.global_coherence  = s->global_coherence;
                vs.total_energy      = s->total_energy;
                vs.births_this_frame = s->births_this_frame;
                vs.deaths_this_frame = s->deaths_this_frame;
                vs.macro_state       = static_cast<MacroState>(s->macro_state);
                vs.cpu_load          = scheduler_.load_ema();
                uint32_t n = std::min(s->num_partials, (uint32_t)MAX_PARTIALS);
                for (uint32_t i = 0; i < n; ++i) {
                    auto& src = s->partials[i];
                    auto& dst = vs.partials[i];
                    dst.frequency         = src.frequency;
                    dst.amplitude         = src.amplitude;
                    dst.spectral_pos      = src.spectral_pos;
                    dst.harmonic_affinity = src.harmonic_affinity;
                    dst.coherence         = src.coherence;
                }
            }
            visual_queue_.write(vs);
        }

        scheduler_.end_frame();
    }
}

// ── Simulation Thread ────────────────────────────────────────────────
void SpectraMorphAudioProcessor::sim_thread_func() {
    while (running_) {
        if (scheduler_.should_skip_simulation()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            continue;
        }

        if (scheduler_.should_physics()) {
            auto* snap = snapshots_.write_buffer();
            for (uint32_t i = 0; i < snap->num_partials; ++i) {
                auto& p = snap->partials[i];
                p.energy *= 0.999f;
                p.age += 1.0f;
                if (p.energy < 0.001f || p.age > p.lifetime_remaining)
                    p.state = ParticleState::Dying;
            }
            snapshots_.commit();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
}

bool SpectraMorphAudioProcessor::hasEditor() const { return true; }

// ── Editor ───────────────────────────────────────────────────────────
juce::AudioProcessorEditor* SpectraMorphAudioProcessor::createEditor() {
    return new SpectraMorphAudioProcessorEditor(*this);
}

void SpectraMorphAudioProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = apvts_.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, dest);
}

void SpectraMorphAudioProcessor::setStateInformation(const void* data, int size) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, size));
    if (xml) apvts_.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new SpectraMorphAudioProcessor();
}
