#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

// Load mono audio file + segment selection (Granular_Synth-style).

class FileSourceManager {
public:
    bool loadFile(const juce::File& file, juce::String& error) {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
        if (!reader) {
            error = "Cannot read audio file";
            return false;
        }

        if (reader->lengthInSamples <= 0) {
            error = "Audio file is empty";
            return false;
        }

        sample_rate_ = reader->sampleRate;
        const int num_ch = static_cast<int>(reader->numChannels);
        const juce::int64 len = reader->lengthInSamples;

        juce::AudioBuffer<float> tmp(static_cast<int>(num_ch),
                                     static_cast<int>(len));
        reader->read(&tmp, 0, static_cast<int>(len), 0, true, true);

        mono_.resize(static_cast<size_t>(len));
        for (juce::int64 i = 0; i < len; ++i) {
            float s = 0.0f;
            for (int ch = 0; ch < num_ch; ++ch)
                s += tmp.getSample(ch, static_cast<int>(i));
            mono_[static_cast<size_t>(i)] = s / static_cast<float>(num_ch);
        }

        file_name_ = file.getFileName();
        playhead_ = 0;
        updateSegmentIndices();
        buildThumbnail();
        return true;
    }

    void setSegmentNormalized(float start, float end) {
        seg_start_norm_ = std::clamp(start, 0.0f, 1.0f);
        seg_end_norm_   = std::clamp(end, 0.0f, 1.0f);
        if (seg_end_norm_ <= seg_start_norm_ + 0.001f)
            seg_end_norm_ = std::min(1.0f, seg_start_norm_ + 0.001f);
        updateSegmentIndices();
    }

    // Move selection preserving length (delta in normalized file coordinates).
    void moveSegmentNormalized(float delta) {
        const float len = seg_end_norm_ - seg_start_norm_;
        if (len <= 0.0f) return;
        float ns = std::clamp(seg_start_norm_ + delta, 0.0f, 1.0f - len);
        setSegmentNormalized(ns, ns + len);
    }

    // Shift by N windows (each window = current segment length). Wraps to start at EOF.
    bool shiftSegmentByWindows(int steps) {
        if (!hasFile() || steps == 0) return false;
        const juce::int64 n = totalSamples();
        const juce::int64 len = segmentLengthSamples();
        if (len <= 0 || len >= n) return false;

        const juce::int64 old_start = seg_start_sample_;
        const juce::int64 old_end = seg_end_sample_;

        juce::int64 ns = seg_start_sample_ + static_cast<juce::int64>(steps) * len;
        juce::int64 ne = ns + len;

        if (steps > 0 && ne > n)
            ns = 0, ne = len;
        else if (steps < 0 && ns < 0) {
            ns = std::max(juce::int64{0}, n - len);
            ne = ns + len;
        }

        const juce::int64 max_start = std::max(juce::int64{0}, n - 1);
        seg_start_norm_ = (max_start > 0)
            ? static_cast<float>(ns) / static_cast<float>(max_start) : 0.0f;
        seg_end_norm_ = static_cast<float>(ne) / static_cast<float>(n);
        updateSegmentIndices();
        return seg_start_sample_ != old_start || seg_end_sample_ != old_end;
    }

    float segmentLengthNorm() const {
        return std::max(0.0f, seg_end_norm_ - seg_start_norm_);
    }

    float segmentStartNorm() const { return seg_start_norm_; }
    float segmentEndNorm()   const { return seg_end_norm_; }

    juce::int64 segmentStartSample() const { return seg_start_sample_; }
    juce::int64 segmentEndSample()   const { return seg_end_sample_; }
    juce::int64 segmentLengthSamples() const {
        return std::max(juce::int64{0}, seg_end_sample_ - seg_start_sample_);
    }

    bool hasFile() const { return !mono_.empty(); }
    double sampleRate() const { return sample_rate_; }
    juce::int64 totalSamples() const { return static_cast<juce::int64>(mono_.size()); }
    const juce::String& fileName() const { return file_name_; }

    const std::vector<float>& thumbnail() const { return thumbnail_; }

    void resetPlayhead() { playhead_.store(seg_start_sample_); }

    void setPlayhead(juce::int64 p) {
        const juce::int64 lo = std::min(seg_start_sample_, seg_end_sample_);
        const juce::int64 hi = std::max(seg_start_sample_, seg_end_sample_);
        playhead_.store(std::clamp(p, lo, hi));
    }

    juce::int64 playhead() const { return playhead_.load(); }

    bool readBlock(int num_samples, float* dest, bool loop_in_place,
                   const std::function<bool()>& on_advance = {})
    {
        if (!hasFile() || num_samples <= 0 || !dest)
            return false;

        juce::int64 pos = playhead_.load();
        for (int i = 0; i < num_samples; ++i) {
            if (pos >= seg_end_sample_) {
                bool continued = false;
                if (on_advance && on_advance())
                    continued = true;
                else if (loop_in_place || on_advance)
                    continued = true;
                if (continued)
                    pos = seg_start_sample_;
                else {
                    dest[i] = 0.0f;
                    for (int j = i + 1; j < num_samples; ++j)
                        dest[j] = 0.0f;
                    playhead_.store(seg_end_sample_);
                    return i > 0;
                }
            }
            dest[i] = mono_[static_cast<size_t>(pos)];
            ++pos;
        }
        playhead_.store(pos);
        return true;
    }

    float sampleAt(juce::int64 index) const {
        if (index < 0 || index >= static_cast<juce::int64>(mono_.size()))
            return 0.0f;
        return mono_[static_cast<size_t>(index)];
    }

private:
    void updateSegmentIndices() {
        const juce::int64 n = static_cast<juce::int64>(mono_.size());
        if (n <= 0) {
            seg_start_sample_ = 0;
            seg_end_sample_ = 0;
            return;
        }

        if (n == 1) {
            seg_start_sample_ = 0;
            seg_end_sample_ = 1;
            return;
        }

        const juce::int64 max_start = n - 1;
        const juce::int64 min_span = std::max(
            juce::int64{1},
            std::min(juce::int64{512}, std::max(juce::int64{1}, n / 1000)));

        seg_start_sample_ = static_cast<juce::int64>(
            seg_start_norm_ * static_cast<float>(max_start));
        seg_end_sample_ = static_cast<juce::int64>(
            seg_end_norm_ * static_cast<float>(n));

        seg_start_sample_ = std::clamp(seg_start_sample_, juce::int64{0}, max_start);
        seg_end_sample_   = std::clamp(seg_end_sample_, juce::int64{1}, n);

        if (seg_end_sample_ <= seg_start_sample_)
            seg_end_sample_ = std::min(n, seg_start_sample_ + min_span);
        if (seg_end_sample_ <= seg_start_sample_)
            seg_end_sample_ = seg_start_sample_ + 1;
        if (seg_end_sample_ > n)
            seg_end_sample_ = n;
    }

    void buildThumbnail() {
        constexpr int thumb_points = 800;
        thumbnail_.assign(thumb_points, 0.0f);
        if (mono_.empty()) return;

        const size_t n = mono_.size();
        for (int i = 0; i < thumb_points; ++i) {
            const size_t a = (static_cast<size_t>(i) * n) / static_cast<size_t>(thumb_points);
            const size_t b = (static_cast<size_t>(i + 1) * n) / static_cast<size_t>(thumb_points);
            float peak = 0.0f;
            for (size_t s = a; s < b && s < n; ++s)
                peak = std::max(peak, std::abs(mono_[s]));
            thumbnail_[static_cast<size_t>(i)] = peak;
        }
    }

    std::vector<float> mono_;
    std::vector<float> thumbnail_;
    double sample_rate_ = 48000.0;
    float seg_start_norm_ = 0.0f;
    float seg_end_norm_   = 1.0f;
    juce::int64 seg_start_sample_ = 0;
    juce::int64 seg_end_sample_ = 0;
    std::atomic<juce::int64> playhead_{0};
    juce::String file_name_;
};
