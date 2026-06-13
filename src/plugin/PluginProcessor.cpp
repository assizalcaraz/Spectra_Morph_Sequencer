#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../dsp/tracking/PeakUtils.h"

#include <algorithm>
#include <cmath>
#include <chrono>

static juce::AudioProcessorValueTreeState::ParameterLayout create_params() {
    juce::AudioProcessorValueTreeState::ParameterLayout params;

    auto add_float = [&](const char* id, const char* name,
                          float min, float max, float def) {
        params.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { id, 1 },
            name,
            juce::NormalisableRange<float>(min, max, 0.01f), def));
    };

    params.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParamID::ProcessMode, 1 },
        "Process Mode",
        juce::StringArray { "Live Insert", "File Granular" }, 0));

    add_float(ParamID::CoherenceChaos, "Coherence / Chaos", 0.0f, 1.0f, 0.2f);
    add_float(ParamID::Density,        "Density",            0.0f, 1.0f, 0.5f);
    add_float(ParamID::TonalResidual, "Tonal / Residual",   0.0f, 1.0f, 0.3f);
    add_float(ParamID::Gravity,        "Gravity",            0.0f, 10.0f, 0.0f);
    add_float(ParamID::Motion,         "Motion",             0.0f, 1.0f, 0.0f);
    add_float(ParamID::Decay,          "Decay",              0.0f, 1.0f, 0.0f);
    add_float(ParamID::Spread,         "Spread",             0.0f, 1.0f, 0.5f);
    add_float(ParamID::DryWet,         "Dry / Wet",          0.0f, 1.0f, 0.5f);
    add_float(ParamID::BirthThreshold, "Birth Threshold",    0.0f, 1.0f, 0.1f);
    add_float(ParamID::MaxPartials,    "Max Partials",       0.0f, 1.0f, 0.5f);
    add_float(ParamID::SegmentStart,   "Segment Start",      0.0f, 1.0f, 0.0f);
    add_float(ParamID::SegmentEnd,     "Segment End",        0.0f, 1.0f, 1.0f);
    add_float(ParamID::TemporalFragmentMs, "Fragment Ms",    20.0f, 200.0f, 80.0f);
    add_float(ParamID::BinScatter,     "Bin Scatter",        0.0f, 1.0f, 0.0f);
    add_float(ParamID::RandomSeed,     "Random Seed",        0.0f, 99999.0f, 42.0f);

    params.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParamID::SpectralQuality, 1 },
        "Spectral Quality",
        juce::StringArray { "Standard 2048", "High 4096" }, 0));

    params.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { ParamID::ExportNormalize, 1 },
        "Export Normalize", true));

    return params;
}

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

ProcessMode SpectraMorphAudioProcessor::current_process_mode() const {
    const int idx = static_cast<int>(
        apvts_.getRawParameterValue(ParamID::ProcessMode)->load());
    return (idx == 1) ? ProcessMode::FileGranular : ProcessMode::LiveInsert;
}

bool SpectraMorphAudioProcessor::isFileGranularMode() const {
    return current_process_mode() == ProcessMode::FileGranular;
}

bool SpectraMorphAudioProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled()
        && layouts.getMainInputChannelSet() != layouts.getMainOutputChannelSet())
        return false;
    return true;
}

void SpectraMorphAudioProcessor::applyFftConfig() {
    const bool high = isFileGranularMode()
        && apvts_.getRawParameterValue(ParamID::SpectralQuality)->load() >= 0.5f;
    fft_size_ = high ? 4096u : 2048u;
    hop_size_ = fft_size_ / 4;
    dry_latency_samples_ = hop_size_ + fft_size_ / 2;
    setLatencySamples(static_cast<int>(dry_latency_samples_));
}

void SpectraMorphAudioProcessor::syncFileSegmentFromApvts() {
    file_source_.setSegmentNormalized(segment_start_.load(), segment_end_.load());
}

uint32_t SpectraMorphAudioProcessor::framesPerFragment() const {
    const float ms = temporal_fragment_ms_.load();
    const float hops_per_sec = static_cast<float>(sample_rate_)
                             / static_cast<float>(hop_size_);
    const float frag_hops = (ms * 0.001f) * hops_per_sec;
    return std::max(2u, static_cast<uint32_t>(frag_hops));
}

uint64_t SpectraMorphAudioProcessor::scrambleSeed() const {
    return static_cast<uint64_t>(random_seed_.load());
}

bool SpectraMorphAudioProcessor::shouldRunSim() const {
    if (isFileGranularMode()) return false;
    const float sim_strength = gravity_.load() + motion_.load()
                             + decay_.load() + spread_.load();
    return sim_strength >= 0.02f;
}

void SpectraMorphAudioProcessor::prepareToPlay(double sampleRate, int blockSize) {
    releaseResources();

    sample_rate_ = sampleRate;
    block_size_  = static_cast<uint32_t>(blockSize);
    applyFftConfig();

    dry_delay_line_.setMaximumDelayInSamples(
        static_cast<int>(dry_latency_samples_) + 64);
    dry_delay_line_.prepare({ sampleRate, static_cast<uint32_t>(blockSize), 1u });
    dry_delay_line_.setDelay(static_cast<float>(dry_latency_samples_));

    scheduler_.set_audio_params(static_cast<float>(sample_rate_), hop_size_);

    fft_.prepare(fft_size_, hop_size_, static_cast<float>(sample_rate_));
    tracker_.prepare(static_cast<float>(sample_rate_), fft_size_, hop_size_);
    resynth_.prepare(static_cast<float>(sample_rate_), fft_size_, hop_size_);

    const uint32_t half_bins = fft_.half_n() + 1;
    work_mag_.resize(half_bins);
    work_phase_.resize(half_bins);
    work_raw_phase_.resize(half_bins);

    uint32_t max_frames = 2048;
    if (file_source_.hasFile()) {
        const juce::int64 seg_len = file_source_.segmentLengthSamples();
        max_frames = static_cast<uint32_t>(
            std::min<juce::int64>(4096, seg_len / static_cast<juce::int64>(hop_size_) + 32));
    }
    frame_store_.prepare(fft_.half_n(), max_frames);

    pool_.clear();
    add_buf_.clear();
    snapshots_.reset();
    sim_snapshots_.reset();
    input_ring_.clear();
    output_ring_.clear();
    visual_queue_.clear();
    dry_delay_line_.reset();
    scheduler_.reset();
    frame_counter_ = 0;
    tracked_f0_ = 0.0f;
    frame_store_.clear();

    syncParamsFromApvts();
    syncFileSegmentFromApvts();

    running_ = true;
    dsp_thread_ = std::thread([this] { dsp_thread_func(); });
    sim_thread_ = std::thread([this] { sim_thread_func(); });
}

void SpectraMorphAudioProcessor::releaseResources() {
    running_ = false;
    file_playing_ = false;
    file_exporting_ = false;
    non_realtime_render_ = false;
    sim_cv_.notify_all();
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
    segment_start_.store(read(ParamID::SegmentStart));
    segment_end_.store(read(ParamID::SegmentEnd));
    temporal_fragment_ms_.store(read(ParamID::TemporalFragmentMs));
    bin_scatter_.store(read(ParamID::BinScatter));
    random_seed_.store(read(ParamID::RandomSeed));
    spectral_quality_.store(read(ParamID::SpectralQuality));
    export_normalize_.store(read(ParamID::ExportNormalize) >= 0.5f ? 1.0f : 0.0f);
}

TransientMode SpectraMorphAudioProcessor::transient_mode_for_coherence(
    float coherence) const
{
    return (coherence < 0.5f) ? TransientMode::Diffuse : TransientMode::Protect;
}

void SpectraMorphAudioProcessor::merge_sim_into_pool() {
    const auto* sim = sim_snapshots_.read();
    if (!sim || sim->num_partials == 0 || sim->frame_number != frame_counter_)
        return;

    for (uint32_t si = 0; si < sim->num_partials; ++si) {
        const auto& sp = sim->partials[si];
        if (sp.amplitude < 1e-8f) continue;

        uint32_t best = MAX_PARTIALS;
        float best_df = 1e9f;
        for (uint32_t pi = 0; pi < MAX_PARTIALS; ++pi) {
            if (!pool_.is_alive(pi)) continue;
            const float df = std::abs(pool_[pi].frequency - sp.frequency);
            if (df < best_df) {
                best_df = df;
                best = pi;
            }
        }
        if (best >= MAX_PARTIALS || best_df > 80.0f) continue;

        auto& p = pool_[best];
        p.frequency    = sp.frequency;
        p.amplitude    = sp.amplitude;
        p.spectral_pos = sp.spectral_pos;
        p.velocity     = sp.velocity;
    }
}

void SpectraMorphAudioProcessor::wait_for_sim_frame(uint32_t frame) {
    const auto timeout = non_realtime_render_.load()
        ? std::chrono::milliseconds(500)
        : std::chrono::milliseconds(8);
    std::unique_lock lock(sim_mutex_);
    sim_cv_.wait_for(lock, timeout, [this, frame] {
        return !running_ || sim_done_frame_.load() >= frame;
    });
}

bool SpectraMorphAudioProcessor::loadSourceFile(
    const juce::File& file, juce::String& error)
{
    if (!file_source_.loadFile(file, error))
        return false;

    syncParamsFromApvts();
    syncFileSegmentFromApvts();
    file_source_.resetPlayhead();
    frame_store_.clear();

    if (getSampleRate() > 0.0) {
        applyFftConfig();
        const uint32_t max_frames = static_cast<uint32_t>(std::min<juce::int64>(
            4096, file_source_.segmentLengthSamples() / static_cast<juce::int64>(hop_size_) + 32));
        frame_store_.prepare(fft_.half_n(), max_frames);
        work_mag_.resize(fft_.half_n() + 1);
        work_phase_.resize(fft_.half_n() + 1);
        work_raw_phase_.resize(fft_.half_n() + 1);
    }
    return true;
}

void SpectraMorphAudioProcessor::setFilePlaying(bool play) {
    if (play) {
        file_source_.resetPlayhead();
        frame_store_.clear();
        frame_counter_ = 0;
    }
    file_playing_ = play;
}

bool SpectraMorphAudioProcessor::renderSegmentToFile(
    const juce::File& dest, juce::String& error)
{
    if (!running_) {
        error = "Plugin not prepared (start playback in host first)";
        return false;
    }
    if (!file_source_.hasFile()) {
        error = "No file loaded";
        return false;
    }

    const double sr = file_source_.sampleRate() > 0.0
        ? file_source_.sampleRate() : sample_rate_;
    const juce::int64 seg_len = file_source_.segmentLengthSamples();
    if (seg_len <= 0) {
        error = "Empty segment";
        return false;
    }

    non_realtime_render_ = true;
    file_exporting_ = true;
    file_playing_ = true;
    frame_store_.clear();
    frame_counter_ = 0;
    file_source_.resetPlayhead();
    input_ring_.clear();
    output_ring_.clear();

    const int block = static_cast<int>(block_size_ > 0 ? block_size_ : 256);
    const juce::int64 tail = static_cast<juce::int64>(dry_latency_samples_) + hop_size_;
    const juce::int64 total = seg_len + tail;

    juce::AudioBuffer<float> out(2, static_cast<int>(total));
    out.clear();

    juce::AudioBuffer<float> tmp(2, block);
    juce::MidiBuffer midi;

    for (juce::int64 pos = 0; pos < total; pos += block) {
        tmp.clear();
        processBlock(tmp, midi);
        const int n = tmp.getNumSamples();
        for (int i = 0; i < n && (pos + i) < total; ++i) {
            out.setSample(0, static_cast<int>(pos + i), tmp.getSample(0, i));
            out.setSample(1, static_cast<int>(pos + i), tmp.getSample(1, i));
        }
    }

    file_playing_ = false;
    file_exporting_ = false;
    non_realtime_render_ = false;

    if (export_normalize_.load() >= 0.5f) {
        float peak = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i)
            peak = std::max(peak, std::abs(out.getSample(0, i)));
        if (peak > 1e-6f) {
            const float g = 0.95f / peak;
            out.applyGain(g);
        }
    }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    if (auto* fmt = fm.findFormatForFileExtension("wav")) {
        std::unique_ptr<juce::FileOutputStream> stream(dest.createOutputStream());
        if (!stream) {
            error = "Cannot open output file";
            return false;
        }
        std::unique_ptr<juce::AudioFormatWriter> writer(
            fmt->createWriterFor(stream.get(), sr, 2, 24, {}, 0));
        if (!writer) {
            error = "Cannot create WAV writer";
            return false;
        }
        stream.release();
        writer->writeFromAudioSampleBuffer(out, 0, out.getNumSamples());
        return true;
    }

    error = "WAV format unavailable";
    return false;
}

void SpectraMorphAudioProcessor::processBlock(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    syncParamsFromApvts();
    syncFileSegmentFromApvts();

    const float mix = dry_wet_.load();
    const bool file_mode = isFileGranularMode();
    const bool feed_file = file_mode
        && (file_playing_.load() || file_exporting_.load())
        && file_source_.hasFile();

    const int ns = buffer.getNumSamples();
    const int nc = buffer.getNumChannels();

    if (file_mode && !feed_file) {
        buffer.clear();
        if (mix < 0.005f)
            return;
    } else if (feed_file) {
        std::vector<float> mono(static_cast<size_t>(ns), 0.0f);
        file_source_.readBlock(ns, mono.data(), true);
        for (int i = 0; i < ns; ++i)
            for (int ch = 0; ch < nc; ++ch)
                buffer.setSample(ch, i, mono[static_cast<size_t>(i)]);
    }

    if (mix < 0.005f && !feed_file)
        return;

    for (int i = 0; i < ns; ++i) {
        float mono = 0.0f;
        for (int ch = 0; ch < nc; ++ch)
            mono += buffer.getReadPointer(ch)[i];
        mono /= static_cast<float>(std::max(1, nc));

        if (!feed_file && file_mode && mix < 0.005f)
            mono = 0.0f;

        input_ring_.write(mono);

        dry_delay_line_.pushSample(0, mono);
        const float dry = dry_delay_line_.popSample(0);

        float wet = 0.0f;
        output_ring_.read(wet);

        const float sample = dry * (1.0f - mix) + wet * mix;

        for (int ch = 0; ch < nc; ++ch)
            buffer.getWritePointer(ch)[i] = sample;
    }
}

void SpectraMorphAudioProcessor::dsp_thread_func() {
    std::vector<float> input_block(hop_size_, 0.0f);
    const uint32_t half_bins = fft_.half_n() + 1;

    while (running_) {
        scheduler_.begin_frame();

        bool got_data = true;
        for (uint32_t i = 0; i < hop_size_; ++i) {
            if (!input_ring_.read(input_block[i])) {
                got_data = false;
                break;
            }
        }

        if (!got_data) {
            resynth_.render_silent_hop(hop_size_);
            const float* out = resynth_.output_buffer();
            for (uint32_t i = 0; i < hop_size_; ++i)
                output_ring_.write(out[i]);

            scheduler_.end_frame();
            scheduler_.update_pressure();
            if (!non_realtime_render_.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        float sum_sq = 0.0f;
        for (uint32_t i = 0; i < hop_size_; ++i)
            sum_sq += input_block[i] * input_block[i];
        const float input_rms = std::sqrt(sum_sq / static_cast<float>(hop_size_));

        fft_.process(input_block.data());
        const float transient_strength = fft_.transient_strength();
        tracker_.set_transient_strength(transient_strength);

        const float coherence = 1.0f - coherence_chaos_.load();
        const float density_val = density_.load();
        const float sr = static_cast<float>(sample_rate_);
        const bool file_mode = isFileGranularMode();

        const float* mag_ptr       = fft_.magnitude();
        const float* phase_ptr     = fft_.phase();
        const float* raw_phase_ptr = fft_.raw_phase();

        if (file_mode) {
            frame_store_.push(mag_ptr, phase_ptr, raw_phase_ptr,
                              input_rms, frame_counter_);
            const uint32_t n_frames = frame_store_.num_frames();
            const uint32_t write_idx = n_frames > 0 ? n_frames - 1 : 0;
            const uint32_t read_idx = TemporalScrambler::map_frame_index(
                write_idx, n_frames, coherence, framesPerFragment(), scrambleSeed());
            telemetry_scramble_read_.store(read_idx);

            frame_store_.copy_frame(read_idx,
                                    work_mag_.data(),
                                    work_phase_.data(),
                                    work_raw_phase_.data());

            if (coherence <= 0.15f) {
                const float scatter_amt = bin_scatter_.load() * (1.0f - coherence);
                TemporalScrambler::scatter_bins(
                    work_mag_.data(), work_phase_.data(), work_raw_phase_.data(),
                    half_bins, scatter_amt,
                    TemporalScrambler::mix_seed(scrambleSeed(), read_idx));
            }

            mag_ptr       = work_mag_.data();
            phase_ptr     = work_phase_.data();
            raw_phase_ptr = work_raw_phase_.data();
        }

        Peak peaks[MAX_PEAKS];
        uint32_t num_peaks = 0;
        const uint32_t half_n = fft_.half_n();
        float f0 = tracked_f0_;

        float threshold = fft_.noise_floor() * birth_threshold_.load() + 0.001f;
        threshold *= 1.0f - coherence * 0.85f;

        float f0_score = 0.0f;

        telemetry_flux_.store(fft_.spectral_flux());
        telemetry_transient_.store(transient_strength);

        if (coherence >= 0.85f) {
            const float hps_f0 = PeakUtils::find_fundamental_hps(
                mag_ptr, half_n, sr, fft_size_, threshold * 0.05f, &f0_score);
            if (hps_f0 >= 40.0f)
                f0 = hps_f0;
            else
                f0 = PeakUtils::find_fundamental_bin(
                    mag_ptr, half_n, sr, fft_size_, threshold * 0.1f);

            tracked_f0_ = PeakUtils::update_f0_ema(tracked_f0_, f0, f0_score);
            if (tracked_f0_ >= 40.0f)
                f0 = tracked_f0_;

            const bool odd_only = PeakUtils::prefer_odd_harmonics(
                mag_ptr, f0, sr, fft_size_, half_n);
            const uint32_t peak_budget = file_mode && coherence >= 0.7f
                ? MAX_PEAKS
                : MAX_PEAKS / 2;
            PeakUtils::build_harmonic_peaks(
                peaks, num_peaks, f0, mag_ptr, raw_phase_ptr, sr, fft_size_, half_n,
                odd_only, density_val, peak_budget);
        } else {
            for (uint32_t i = 1; i < half_n - 1 && num_peaks < MAX_PEAKS; ++i) {
                if (mag_ptr[i] > mag_ptr[i - 1] && mag_ptr[i] > mag_ptr[i + 1]
                    && mag_ptr[i] > threshold) {
                    auto& p = peaks[num_peaks++];
                    p.bin_index = static_cast<uint16_t>(i);
                    p.frequency = PeakUtils::refine_frequency(i, mag_ptr, sr, fft_size_);
                    p.magnitude = mag_ptr[i];
                    p.phase     = phase_ptr[i];
                }
            }

            if (num_peaks > 0) {
                f0 = PeakUtils::estimate_fundamental(peaks, num_peaks);
                PeakUtils::enrich_harmonics(
                    peaks, num_peaks, f0, mag_ptr, phase_ptr, sr, fft_size_, half_n,
                    threshold, coherence, MAX_PEAKS);
            }

            if (num_peaks > 1) {
                std::sort(peaks, peaks + num_peaks,
                    [](const Peak& a, const Peak& b) {
                        return a.magnitude > b.magnitude;
                    });
                float cap_lo = 4.0f + density_val * 80.0f;
                float cap_hi = 24.0f + density_val * 200.0f;
                if (file_mode)
                    cap_hi = 48.0f + density_val * 400.0f;
                const uint32_t peak_cap = static_cast<uint32_t>(
                    cap_lo + coherence * (cap_hi - cap_lo));
                num_peaks = std::min(num_peaks, std::max(8u, peak_cap));
            }
        }

        if (coherence >= 0.85f && num_peaks > 1) {
            uint32_t pool_cap = std::max(16u,
                2u + static_cast<uint32_t>(
                    density_val * static_cast<float>(MAX_PARTIALS - 2)));
            if (file_mode)
                pool_cap = std::max(pool_cap, 64u);
            num_peaks = std::min(num_peaks, pool_cap);
        }

        if (f0 >= 40.0f && tracked_f0_ < 40.0f)
            tracked_f0_ = f0;
        telemetry_f0_.store(tracked_f0_);

        ++frame_counter_;
        tracker_.set_coherence(coherence);
        tracker_.set_fundamental(tracked_f0_);
        const bool run_tracking = (coherence >= 0.85f)
            || scheduler_.tick(frame_counter_);

        if (run_tracking) {
            if (coherence >= 0.85f)
                tracker_.sync_faithful(
                    peaks, num_peaks, pool_, frame_counter_, tracked_f0_);
            else
                tracker_.track(
                    peaks, num_peaks, pool_, frame_counter_, tracked_f0_);

            if (coherence < 0.85f) {
                const uint32_t cap_user = 16u + static_cast<uint32_t>(
                    max_partials_.load() * static_cast<float>(MAX_PARTIALS - 16));
                const uint32_t cap_density = 8u + static_cast<uint32_t>(
                    density_val * static_cast<float>(MAX_PARTIALS - 8));
                const uint32_t effective_cap = std::min(
                    cap_user, std::min(cap_density, scheduler_.max_partials()));
                pool_.prune_to(effective_cap);
            }
        }

        const bool run_sim = shouldRunSim();

        {
            auto* pre = snapshots_.write_buffer();
            pool_.write_snapshot(*pre, frame_counter_);
            snapshots_.commit();
        }

        if (run_sim) {
            {
                std::lock_guard lock(sim_mutex_);
                dsp_ready_frame_ = frame_counter_;
                sim_done_frame_.store(0);
                sim_cv_.notify_one();
            }
            wait_for_sim_frame(frame_counter_);
            merge_sim_into_pool();
        }

        {
            auto* snap = snapshots_.write_buffer();
            pool_.write_snapshot(*snap, frame_counter_);
            PhaseManager::apply_to_snapshot(*snap, tracked_f0_, coherence, phase_seed_);
            snapshots_.commit();
        }

        const auto* snap = snapshots_.read();
        float tonal = std::clamp(1.0f - tonal_residual_.load(), 0.0f, 1.0f);
        if (file_mode && coherence >= 0.7f)
            tonal = std::max(tonal, 0.7f);
        const TransientMode tmode = transient_mode_for_coherence(coherence);

        const float mix = dry_wet_.load();
        if (snap && snap->num_partials > 0) {
            resynth_.render(*snap, fft_, tonal, spread_.load(), coherence,
                            transient_strength, tmode, tracked_f0_,
                            input_block.data(), input_rms, mix);
        } else {
            resynth_.render_silent_hop(hop_size_);
        }

        const float* out = resynth_.output_buffer();
        for (uint32_t i = 0; i < hop_size_; ++i)
            output_ring_.write(out[i]);

        {
            VisualState vs{};
            if (snap) {
                vs.frame_number      = snap->frame_number;
                vs.num_partials      = snap->num_partials;
                vs.global_coherence  = snap->global_coherence;
                vs.total_energy      = snap->total_energy;
                vs.births_this_frame = snap->births_this_frame;
                vs.deaths_this_frame = snap->deaths_this_frame;
                vs.macro_state       = static_cast<MacroState>(snap->macro_state);
                vs.cpu_load          = scheduler_.load_ema();
                const uint32_t n = std::min(snap->num_partials, (uint32_t)MAX_PARTIALS);
                for (uint32_t i = 0; i < n; ++i) {
                    auto& src = snap->partials[i];
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

void SpectraMorphAudioProcessor::sim_thread_func() {
    while (running_) {
        uint32_t frame = 0;
        {
            std::unique_lock lock(sim_mutex_);
            sim_cv_.wait(lock, [this] {
                return !running_ || dsp_ready_frame_ != 0;
            });
            if (!running_) break;
            frame = dsp_ready_frame_;
            dsp_ready_frame_ = 0;
        }

        const auto* canon = snapshots_.read();
        if (canon && canon->frame_number == frame && canon->num_partials > 0) {
            SimParams params;
            params.gravity = gravity_.load();
            params.motion  = motion_.load();
            params.decay   = decay_.load();
            params.spread  = spread_.load();

            auto* out = sim_snapshots_.write_buffer();
            simulator_.step(*canon, *out, params, sim_rng_seed_);
            out->frame_number = frame;
            sim_snapshots_.commit();
            sim_done_frame_.store(frame);
            sim_cv_.notify_all();
        } else {
            sim_done_frame_.store(frame);
            sim_cv_.notify_all();
        }
    }
}

bool SpectraMorphAudioProcessor::hasEditor() const { return true; }

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
