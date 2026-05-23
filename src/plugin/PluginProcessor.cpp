#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../dsp/tracking/PeakUtils.h"

#include <algorithm>
#include <cmath>

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
    add_float(ParamID::TonalResidual, "Tonal / Residual",   0.0f, 1.0f, 0.3f);
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

    scheduler_.set_audio_params(static_cast<float>(sample_rate_), hop_size_);

    // Init pipeline
    fft_.prepare(fft_size_, hop_size_, static_cast<float>(sample_rate_));
    tracker_.prepare(static_cast<float>(sample_rate_), fft_size_, hop_size_);
    resynth_.prepare(static_cast<float>(sample_rate_), fft_size_, hop_size_);

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

void SpectraMorphAudioProcessor::syncParamsFromApvts() {
    auto read = [this](const char* id) {
        return apvts_.getRawParameterValue(id)->load();
    };

    dry_wet_.store(read(ParamID::DryWet));
    coherence_chaos_.store(read(ParamID::CoherenceChaos));
    density_.store(read(ParamID::Density));
    tonal_residual_.store(read(ParamID::TonalResidual));
    gravity_.store(read(ParamID::Gravity));
    motion_.store(read(ParamID::Motion));
    decay_.store(read(ParamID::Decay));
    spread_.store(read(ParamID::Spread));
    birth_threshold_.store(read(ParamID::BirthThreshold));
    max_partials_.store(read(ParamID::MaxPartials));
}

void SpectraMorphAudioProcessor::applyParticleEffects() {
    // Knob left (0) = coherence, right (1) = chaos
    const float coherence = 1.0f - coherence_chaos_.load();
    const float decay     = decay_.load();
    const float density   = density_.load();

    // High coherence: preserve spectrum, skip ecological drift
    if (coherence < 0.85f) {
        const float chaos   = 1.0f - coherence;
        const float gravity = gravity_.load();
        const float motion  = motion_.load();
        const float energy_decay = 1.0f - decay * 0.08f;
        const float amp_decay    = 1.0f - decay * 0.04f;

        for (uint32_t i = 0; i < MAX_PARTIALS; ++i) {
            if (!pool_.is_alive(i)) continue;

            auto& p = pool_[i];
            p.energy    *= energy_decay;
            p.amplitude *= amp_decay;

            const float pull = (5.0f - p.spectral_pos) * gravity * 0.01f;
            p.velocity += (chaos - 0.5f) * motion * 0.2f;
            p.velocity  = std::clamp(p.velocity, -2.0f, 2.0f);
            p.spectral_pos += pull + p.velocity * motion * 0.1f;
            p.spectral_pos = std::clamp(p.spectral_pos, 0.0f, LOG_OCTAVES);
            p.frequency = 20.0f * std::pow(2.0f, p.spectral_pos);

            p.coherence *= 1.0f - chaos * 0.03f;
            if (p.coherence < 0.05f) p.coherence = 0.05f;
        }
    } else if (decay > 0.01f) {
        const float amp_decay = 1.0f - decay * 0.02f;
        for (uint32_t i = 0; i < MAX_PARTIALS; ++i) {
            if (!pool_.is_alive(i)) continue;
            pool_[i].amplitude *= amp_decay;
            pool_[i].energy *= 1.0f - decay * 0.03f;
        }
    }

    if (coherence < 0.85f) {
        const uint32_t density_target = 8u + static_cast<uint32_t>(
            density * static_cast<float>(MAX_PARTIALS - 8));
        pool_.prune_to(density_target);
    }
}

void SpectraMorphAudioProcessor::processBlock(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    syncParamsFromApvts();

    float mix = dry_wet_.load();

    // True bypass: no processing, no ring I/O
    if (mix < 0.005f)
        return;

    int ns = buffer.getNumSamples();
    int nc = buffer.getNumChannels();

    // Sum input to mono
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
            scheduler_.update_pressure();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        // Compute input RMS for amplitude envelope matching
        float sum_sq = 0.0f;
        for (uint32_t i = 0; i < hop_size_; ++i)
            sum_sq += input_block[i] * input_block[i];
        const float input_rms = std::sqrt(sum_sq / static_cast<float>(hop_size_));

        // 1. FFT
        fft_.process(input_block.data());

        // 2. Peak detection (sub-bin refinement)
        Peak peaks[MAX_PEAKS];
        uint32_t num_peaks = 0;
        uint32_t half_n = fft_.half_n();
        const float coherence = 1.0f - coherence_chaos_.load();
        const float density_val = density_.load();
        const float sr = static_cast<float>(sample_rate_);

        float threshold = fft_.noise_floor() * birth_threshold_.load() + 0.001f;
        threshold *= 1.0f - coherence * 0.85f;

        const float* mag   = fft_.magnitude();
        const float* phase = fft_.phase();

        if (coherence >= 0.85f) {
            float f0 = PeakUtils::find_fundamental_hps(
                mag, half_n, sr, fft_size_, threshold * 0.05f);
            if (f0 < 40.0f)
                f0 = PeakUtils::find_fundamental_bin(
                    mag, half_n, sr, fft_size_, threshold * 0.1f);

            const bool odd_only = PeakUtils::prefer_odd_harmonics(
                mag, f0, sr, fft_size_, half_n);
            PeakUtils::build_harmonic_peaks(
                peaks, num_peaks, f0, mag, phase, sr, fft_size_, half_n,
                odd_only, MAX_PEAKS);
        } else {
            for (uint32_t i = 1; i < half_n - 1 && num_peaks < MAX_PEAKS; ++i) {
                if (mag[i] > mag[i - 1] && mag[i] > mag[i + 1] && mag[i] > threshold) {
                    auto& p = peaks[num_peaks++];
                    p.bin_index = static_cast<uint16_t>(i);
                    p.frequency = PeakUtils::refine_frequency(i, mag, sr, fft_size_);
                    p.magnitude = mag[i];
                    p.phase     = phase[i];
                }
            }

            if (num_peaks > 0) {
                const float f0 = PeakUtils::estimate_fundamental(peaks, num_peaks);
                PeakUtils::enrich_harmonics(
                    peaks, num_peaks, f0, mag, phase, sr, fft_size_, half_n,
                    threshold, coherence, MAX_PEAKS);
            }

            if (num_peaks > 1) {
                std::sort(peaks, peaks + num_peaks,
                    [](const Peak& a, const Peak& b) {
                        return a.magnitude > b.magnitude;
                    });
                const float cap_lo = 4.0f + density_val * 80.0f;
                const float cap_hi = 24.0f + density_val * 200.0f;
                const uint32_t peak_cap = static_cast<uint32_t>(
                    cap_lo + coherence * (cap_hi - cap_lo));
                num_peaks = std::min(num_peaks, std::max(8u, peak_cap));
            }
        }

        if (coherence >= 0.85f && num_peaks > 1) {
            const uint32_t peak_cap = static_cast<uint32_t>(
                48.0f + density_val * static_cast<float>(MAX_PARTIALS - 48));
            num_peaks = std::min(num_peaks, peak_cap);
        }

        // 4. Partial tracking
        ++frame_counter_;
        tracker_.set_coherence(coherence);
        const bool run_tracking = (coherence >= 0.85f)
            || scheduler_.tick(frame_counter_);

        if (run_tracking) {
            if (coherence >= 0.85f)
                tracker_.sync_faithful(peaks, num_peaks, pool_, frame_counter_);
            else {
                tracker_.track(peaks, num_peaks, pool_, frame_counter_);
                applyParticleEffects();

                const uint32_t cap_user = 16u + static_cast<uint32_t>(
                    max_partials_.load() * static_cast<float>(MAX_PARTIALS - 16));
                const uint32_t cap_density = 8u + static_cast<uint32_t>(
                    density_val * static_cast<float>(MAX_PARTIALS - 8));
                const uint32_t effective_cap = std::min(
                    cap_user, std::min(cap_density, scheduler_.max_partials()));
                pool_.prune_to(effective_cap);
            }
        }

        // 5. Write snapshot
        {
            auto* snap = snapshots_.write_buffer();
            pool_.write_snapshot(*snap, frame_counter_);
            snapshots_.commit();
        }

        // 6. Resynthesis
        const auto* snap = snapshots_.read();
        const float tonal = 1.0f - tonal_residual_.load();
        if (snap && snap->num_partials > 0) {
            resynth_.render(*snap, fft_, tonal, spread_.load(), coherence,
                            input_block.data(), input_rms);
        } else {
            resynth_.render_passthrough(input_block.data(), hop_size_);
        }
        const float* out = resynth_.output_buffer();
        for (uint32_t i = 0; i < hop_size_; ++i)
            output_ring_.write(out[i]);

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
        scheduler_.update_pressure();
    }
}

// ── Simulation Thread (paused — SPECS_12 ownership TBD) ─────────────
void SpectraMorphAudioProcessor::sim_thread_func() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
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
