#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "WindowPatternController.h"
#include "BuildVersion.h"
#include "../core/types/Types.h"
#include <algorithm>
#include <memory>

namespace {

juce::String build_version_label() {
    return juce::String("v") + sm::build::version
         + " | " + sm::build::tag
         + " | " + sm::build::git_sha;
}

void paint_version_badge(juce::Graphics& g, juce::Rectangle<int> area) {
    g.setFont(juce::FontOptions(11.0f));
    g.setColour(juce::Colours::orange.withAlpha(0.92f));
    g.drawText(build_version_label(), area,
               juce::Justification::centredRight);
}

constexpr float kMinViewSpan = 0.01f;

} // namespace

namespace {

constexpr int kWaveformPad = 4;
constexpr int kStepStripH  = 22;

void stylePatternStepButton(juce::TextButton& btn, int step, int cur, bool active)
{
    if (!active) {
        btn.setColour(juce::TextButton::buttonOnColourId,
                      juce::Colours::black.withAlpha(0.55f));
        btn.setColour(juce::TextButton::buttonColourId,
                      juce::Colours::black.withAlpha(0.35f));
        btn.setColour(juce::TextButton::textColourOffId,
                      juce::Colours::grey.withAlpha(0.7f));
        btn.setColour(juce::TextButton::textColourOnId,
                      juce::Colours::grey.withAlpha(0.7f));
    } else if (step == cur) {
        btn.setColour(juce::TextButton::buttonOnColourId,
                      juce::Colours::orange.darker(0.1f));
        btn.setColour(juce::TextButton::buttonColourId,
                      juce::Colours::orange.darker(0.25f));
        btn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        btn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    } else {
        btn.setColour(juce::TextButton::buttonOnColourId,
                      juce::Colours::darkgrey.brighter(0.25f));
        btn.setColour(juce::TextButton::buttonColourId,
                      juce::Colours::darkgrey.brighter(0.05f));
        btn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        btn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    }
}

} // namespace

PatternGridView::PatternGridView(SpectraMorphAudioProcessor& p, WaveformSegmentView& wave)
    : processor_(p)
    , waveform_(wave)
{
    setInterceptsMouseClicks(false, true);
}

void PatternGridView::refresh() {
    rebuildIfNeeded();
    last_view_start_ = -1.0f;
    last_view_end_ = -1.0f;
    last_width_ = -1;
    layoutTimelineSteps();

    const int cur = processor_.patternStepIndex();
    const uint32_t mask = processor_.patternMask();
    last_current_ = cur;
    last_mask_ = mask;

    for (size_t i = 0; i < step_buttons_.size(); ++i) {
        auto* btn = step_buttons_[i].get();
        const bool active = WindowPattern::isStepActive(
            static_cast<uint32_t>(i), mask,
            static_cast<uint32_t>(step_buttons_.size()));
        btn->setToggleState(active, juce::dontSendNotification);
        stylePatternStepButton(*btn, static_cast<int>(i), cur, active);
    }
}

void PatternGridView::rebuildIfNeeded() {
    const uint32_t sc = processor_.patternStepCount();
    if (sc == last_step_count_ && sc == step_buttons_.size())
        return;

    last_step_count_ = sc;
    last_mask_ = ~0u;
    last_current_ = -1;
    step_buttons_.clear();
    removeAllChildren();

    for (uint32_t i = 0; i < sc; ++i) {
        auto btn = std::make_unique<juce::TextButton>(juce::String(i + 1));
        btn->setClickingTogglesState(false);
        const int step = static_cast<int>(i);
        btn->onClick = [this, step] {
            const uint32_t sc = processor_.patternStepCount();
            const uint32_t mask = processor_.patternMask();
            const bool active = WindowPattern::isStepActive(
                static_cast<uint32_t>(step), mask, sc);
            processor_.setPatternStepActive(step, !active);
            processor_.jumpPatternStep(step);
            refresh();
        };
        btn->setToggleState(
            WindowPattern::isStepActive(i, processor_.patternMask(), sc),
            juce::dontSendNotification);
        addAndMakeVisible(btn.get());
        stylePatternStepButton(*btn, step,
            processor_.patternStepIndex(), btn->getToggleState());
        step_buttons_.push_back(std::move(btn));
    }
}

void PatternGridView::layoutTimelineSteps() {
    const auto& src = processor_.file_source();
    if (!src.hasFile() || step_buttons_.empty())
        return;

    const juce::int64 total = src.totalSamples();
    const juce::int64 len = src.segmentLengthSamples();
    if (total <= 0 || len <= 0)
        return;

    const float vs = waveform_.viewStartNorm();
    const float ve = waveform_.viewEndNorm();
    const int w = getWidth();
    if (w <= 0)
        return;

    if (vs == last_view_start_ && ve == last_view_end_ && w == last_width_
        && last_step_count_ == step_buttons_.size())
        return;

    last_view_start_ = vs;
    last_view_end_ = ve;
    last_width_ = w;

    const int btn_y = getHeight() - kStepStripH - kWaveformPad - 14;
    const int btn_h = kStepStripH;

    for (size_t i = 0; i < step_buttons_.size(); ++i) {
        const juce::int64 ss = static_cast<juce::int64>(i) * len;
        const juce::int64 se = std::min(ss + len, total);
        const float f0 = static_cast<float>(ss) / static_cast<float>(total);
        const float f1 = static_cast<float>(se) / static_cast<float>(total);

        if (f1 < vs || f0 > ve) {
            step_buttons_[i]->setBounds(0, 0, 0, 0);
            continue;
        }

        const float x0 = waveform_.fileNormToX(f0) + static_cast<float>(kWaveformPad);
        const float x1 = waveform_.fileNormToX(f1) + static_cast<float>(kWaveformPad);
        int bx = static_cast<int>(std::floor(x0));
        int bw = static_cast<int>(std::ceil(x1)) - bx - 1;
        bw = juce::jmax(bw, 6);

        step_buttons_[i]->setBounds(bx, btn_y, bw, btn_h);
        step_buttons_[i]->setButtonText(bw >= 16 ? juce::String(i + 1) : juce::String());
    }
}

void PatternGridView::resized() {
    last_view_start_ = -1.0f;
    last_view_end_ = -1.0f;
    last_width_ = -1;
    layoutTimelineSteps();
}

void PatternGridView::paint(juce::Graphics&) {}

WaveformSegmentView::WaveformSegmentView(SpectraMorphAudioProcessor& p)
    : processor_(p)
{
    setWantsKeyboardFocus(true);
    pattern_grid_ = std::make_unique<PatternGridView>(p, *this);
    addAndMakeVisible(*pattern_grid_);
}

void WaveformSegmentView::refresh() {
    if (pattern_grid_) {
        pattern_grid_->setVisible(processor_.file_source().hasFile());
        pattern_grid_->refresh();
    }
    repaint();
}

float WaveformSegmentView::fileNormToX(float file_norm) const {
    return xFromFileNorm(file_norm);
}

void WaveformSegmentView::resetView() {
    view_start_ = 0.0f;
    view_end_ = 1.0f;
}

void WaveformSegmentView::setViewRange(float start, float end) {
    start = std::clamp(start, 0.0f, 1.0f);
    end   = std::clamp(end, 0.0f, 1.0f);
    if (end <= start + kMinViewSpan) {
        const float mid = (start + end) * 0.5f;
        start = std::max(0.0f, mid - kMinViewSpan * 0.5f);
        end   = std::min(1.0f, start + kMinViewSpan);
        start = std::max(0.0f, end - kMinViewSpan);
    }
    view_start_ = start;
    view_end_   = end;
}

void WaveformSegmentView::zoomIn() {
    const float span = view_end_ - view_start_;
    const float center = (view_start_ + view_end_) * 0.5f;
    setViewRange(center - span * 0.25f, center + span * 0.25f);
}

void WaveformSegmentView::zoomOut() {
    const float span = view_end_ - view_start_;
    const float center = (view_start_ + view_end_) * 0.5f;
    if (span >= 0.999f) {
        resetView();
        return;
    }
    setViewRange(center - span, center + span);
}

void WaveformSegmentView::zoomToSelection() {
    const auto& src = processor_.file_source();
    if (!src.hasFile()) return;
    const float s0 = src.segmentStartNorm();
    const float s1 = src.segmentEndNorm();
    const float pad = std::max(0.005f, (s1 - s0) * 0.35f);
    setViewRange(s0 - pad, s1 + pad);
}

float WaveformSegmentView::viewNormFromX(int x) const {
    const int w = getWidth();
    if (w <= 0) return 0.0f;
    return std::clamp(static_cast<float>(x) / static_cast<float>(w), 0.0f, 1.0f);
}

float WaveformSegmentView::fileNormFromX(int x) const {
    const float vn = viewNormFromX(x);
    return view_start_ + vn * (view_end_ - view_start_);
}

float WaveformSegmentView::xFromFileNorm(float file_norm) const {
    const int w = getWidth();
    if (w <= 0) return 0.0f;
    const float span = view_end_ - view_start_;
    if (span <= 1e-6f) return 0.0f;
    const float vn = (file_norm - view_start_) / span;
    return vn * static_cast<float>(w);
}

void WaveformSegmentView::applyMarqueeSelection() {
    const float s0 = std::min(marquee_start_, marquee_end_);
    const float s1 = std::max(marquee_start_, marquee_end_);
    constexpr float kMinSpan = 0.005f;
    if (s1 - s0 < kMinSpan) return;
    processor_.setFileSegmentNormalized(s0, s1);
}

void WaveformSegmentView::resized() {
    if (pattern_grid_) {
        pattern_grid_->setBounds(getLocalBounds());
        pattern_grid_->toFront(false);
    }
}

SimulationView::SimulationView(SpectraMorphAudioProcessor& p)
    : processor_(p)
{
    setOpaque(false);
    startTimerHz(30);
}

void SimulationView::resized() {
    repaint();
}

void SimulationView::paint(juce::Graphics& g) {
    const auto area = getLocalBounds();
    g.fillAll(juce::Colour(0xe0101010));

    constexpr int kKnobRowH = 118;
    const auto knob_band = area.withHeight(kKnobRowH)
                                .withY(area.getBottom() - kKnobRowH);
    g.setColour(juce::Colour(0xd8121212));
    g.fillRect(knob_band);

    const auto viz_area = area.reduced(2);
    g.setColour(juce::Colours::darkgrey.withAlpha(0.22f));
    g.drawRoundedRectangle(viz_area.toFloat(), 3.0f, 1.0f);

    VisualState vs;
    const bool has_data = processor_.read_visual(vs);

    if (has_data && vs.num_partials > 0) {
        auto viz = viz_area.toFloat().reduced(4.0f);
        const float h = viz.getHeight();
        const float w = viz.getWidth();
        const float cy = viz.getCentreY();

        for (uint32_t i = 0; i < vs.num_partials; ++i) {
            auto& pt = vs.partials[i];
            if (pt.amplitude < 0.005f) continue;
            const float x = viz.getX() + (pt.spectral_pos / LOG_OCTAVES) * w;
            const float y = cy + (pt.frequency / 5000.0f - 0.5f) * h * 0.75f;
            const float r = 1.5f + pt.amplitude * 5.5f;
            const float hue = 0.6f - pt.harmonic_affinity * 0.5f;
            g.setColour(juce::Colour::fromHSV(hue, 0.62f, 0.72f,
                                              pt.coherence * 0.42f + 0.1f));
            g.fillEllipse(x - r, y - r, r * 2.0f, r * 2.0f);
        }
    }

    g.setFont(juce::FontOptions(22.0f).withStyle("Bold"));
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.drawText("4ssiiz", area.reduced(8), juce::Justification::bottomRight);
}

void WaveformSegmentView::paintStepGrid(juce::Graphics& g,
                                        juce::Rectangle<float> area,
                                        float view_span)
{
    const auto& src = processor_.file_source();
    const juce::int64 total = src.totalSamples();
    const juce::int64 len = src.segmentLengthSamples();
    if (total <= 0 || len <= 0)
        return;

    const uint32_t sc = WindowPattern::stepCount(total, len);
    for (uint32_t i = 1; i < sc; ++i) {
        const float f = static_cast<float>(static_cast<juce::int64>(i) * len)
                      / static_cast<float>(total);
        if (f < view_start_ || f > view_end_)
            continue;
        const float x = area.getX() + (f - view_start_) / view_span * area.getWidth();
        g.setColour(juce::Colours::white.withAlpha(0.1f));
        g.drawVerticalLine(static_cast<int>(x), area.getY(), area.getBottom() - 18.0f);
    }
}

void WaveformSegmentView::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1a1a1a));
    const auto& src = processor_.file_source();
    if (!src.hasFile()) {
        g.setColour(juce::Colours::grey);
        g.drawText("Load a WAV file", getLocalBounds(),
                   juce::Justification::centred);
        return;
    }

    const auto& thumb = src.thumbnail();
    if (thumb.empty()) return;

    auto area = getLocalBounds().reduced(4).toFloat();
    const float w = area.getWidth();
    const float h = area.getHeight();
    const float mid = area.getCentreY();
    const float view_span = view_end_ - view_start_;

    g.setColour(juce::Colours::darkgrey);
    for (size_t i = 0; i < thumb.size(); ++i) {
        const float file_norm = (static_cast<float>(i) + 0.5f)
                              / static_cast<float>(thumb.size());
        if (file_norm < view_start_ || file_norm > view_end_)
            continue;
        const float vn = (file_norm - view_start_) / view_span;
        const float x = area.getX() + vn * w;
        const float amp = thumb[i] * h * 0.45f;
        g.drawVerticalLine(static_cast<int>(x), mid - amp, mid + amp);
    }

    paintStepGrid(g, area, view_span);

    const float s0 = src.segmentStartNorm();
    const float s1 = src.segmentEndNorm();
    const float x0 = area.getX() + (s0 - view_start_) / view_span * w;
    const float x1 = area.getX() + (s1 - view_start_) / view_span * w;

    if (drag_ == DragMode::Marquee) {
        const float mx0 = area.getX()
            + (std::min(marquee_start_, marquee_end_) - view_start_) / view_span * w;
        const float mx1 = area.getX()
            + (std::max(marquee_start_, marquee_end_) - view_start_) / view_span * w;
        g.setColour(juce::Colours::cyan.withAlpha(0.2f));
        g.fillRect(mx0, area.getY(), mx1 - mx0, area.getHeight());
        g.setColour(juce::Colours::cyan);
        g.drawVerticalLine(static_cast<int>(mx0), area.getY(), area.getBottom());
        g.drawVerticalLine(static_cast<int>(mx1), area.getY(), area.getBottom());
    }

    if (s1 >= view_start_ && s0 <= view_end_) {
        const float vis_x0 = std::max(area.getX(), x0);
        const float vis_x1 = std::min(area.getRight(), x1);
        if (vis_x1 > vis_x0) {
            g.setColour(juce::Colours::orange.withAlpha(0.25f));
            g.fillRect(vis_x0, area.getY(), vis_x1 - vis_x0, area.getHeight());
        }
    }

    constexpr float handle_w = 6.0f;
    g.setColour(juce::Colours::orange);
    if (s0 >= view_start_ && s0 <= view_end_) {
        g.drawVerticalLine(static_cast<int>(x0), area.getY(), area.getBottom());
        g.fillRect(x0 - handle_w * 0.5f, area.getY(), handle_w, area.getHeight());
    }
    if (s1 >= view_start_ && s1 <= view_end_) {
        g.drawVerticalLine(static_cast<int>(x1), area.getY(), area.getBottom());
        g.fillRect(x1 - handle_w * 0.5f, area.getY(), handle_w, area.getHeight());
    }

    if (src.totalSamples() > 0) {
        const float ph = static_cast<float>(src.playhead())
                       / static_cast<float>(src.totalSamples());
        if (ph >= view_start_ && ph <= view_end_) {
            const float px = area.getX() + (ph - view_start_) / view_span * w;
            g.setColour(juce::Colours::white.withAlpha(0.65f));
            g.drawVerticalLine(static_cast<int>(px), area.getY(), area.getBottom());
        }
    }

    if (view_span < 0.999f) {
        g.setFont(juce::FontOptions(9.0f));
        g.setColour(juce::Colours::lightgrey.withAlpha(0.7f));
        g.drawText(juce::String(static_cast<int>(view_span * 100.0f)) + "% view",
                   area.toNearestInt().removeFromTop(12),
                   juce::Justification::centredRight);
    }

    g.setFont(juce::FontOptions(10.0f));
    g.setColour(juce::Colours::grey);
    g.drawText("centro: mover  |  bordes: duracion  |  fuera: seleccion  |  rueda: zoom",
               area.toNearestInt().removeFromBottom(14),
               juce::Justification::centred);
}

void WaveformSegmentView::mouseDown(const juce::MouseEvent& e) {
    if (!processor_.file_source().hasFile()) return;
    const int w = getWidth();
    if (w <= 0) return;

    grabKeyboardFocus();
    const float file_norm = fileNormFromX(e.x);
    const float s0 = processor_.file_source().segmentStartNorm();
    const float s1 = processor_.file_source().segmentEndNorm();
    const float x0 = xFromFileNorm(s0);
    const float x1 = xFromFileNorm(s1);
    constexpr float handle_px = 12.0f;
    const float mx = static_cast<float>(e.x);

    drag_anchor_norm_ = file_norm;
    seg0_at_down_ = s0;
    seg1_at_down_ = s1;

    if (mx >= x0 + handle_px && mx <= x1 - handle_px && (s1 - s0) > 0.01f) {
        drag_ = DragMode::Move;
    } else if (std::abs(mx - x0) <= handle_px) {
        drag_ = DragMode::ResizeStart;
    } else if (std::abs(mx - x1) <= handle_px) {
        drag_ = DragMode::ResizeEnd;
    } else {
        drag_ = DragMode::Marquee;
        marquee_start_ = file_norm;
        marquee_end_ = file_norm;
    }
}

void WaveformSegmentView::mouseDrag(const juce::MouseEvent& e) {
    if (drag_ == DragMode::None) return;
    const float file_norm = fileNormFromX(e.x);

    if (drag_ == DragMode::Move) {
        const float len = seg1_at_down_ - seg0_at_down_;
        const float delta = file_norm - drag_anchor_norm_;
        float ns = std::clamp(seg0_at_down_ + delta, 0.0f, 1.0f - len);
        processor_.setFileSegmentNormalized(ns, ns + len);
    } else if (drag_ == DragMode::ResizeStart) {
        const float end = processor_.file_source().segmentEndNorm();
        float ns = std::min(file_norm, end - 0.005f);
        processor_.setFileSegmentNormalized(std::max(0.0f, ns), end);
    } else if (drag_ == DragMode::ResizeEnd) {
        const float start = processor_.file_source().segmentStartNorm();
        float ne = std::max(file_norm, start + 0.005f);
        processor_.setFileSegmentNormalized(start, std::min(1.0f, ne));
    } else if (drag_ == DragMode::Marquee) {
        marquee_end_ = file_norm;
    }
    refresh();
}

void WaveformSegmentView::mouseUp(const juce::MouseEvent&) {
    if (drag_ == DragMode::Marquee)
        applyMarqueeSelection();
    drag_ = DragMode::None;
    refresh();
}

void WaveformSegmentView::mouseWheelMove(const juce::MouseEvent&,
                                         const juce::MouseWheelDetails& wheel) {
    if (!processor_.file_source().hasFile()) return;
    if (wheel.deltaY > 0.0f)
        zoomIn();
    else if (wheel.deltaY < 0.0f)
        zoomOut();
    refresh();
}

bool WaveformSegmentView::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::leftKey) {
        processor_.shiftFileSegmentByWindows(-1);
        refresh();
        return true;
    }
    if (key == juce::KeyPress::rightKey) {
        processor_.shiftFileSegmentByWindows(1);
        refresh();
        return true;
    }
    return false;
}

SpectraMorphAudioProcessorEditor::SpectraMorphAudioProcessorEditor(
    SpectraMorphAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , processor_(p)
    , simulation_(p)
    , waveform_(p)
{
    setResizable(true, true);
    setResizeLimits(760, 520, 4096, 4096);
    setSize(default_size_.x, default_size_.y);
    startTimerHz(30);

    addAndMakeVisible(play_tab_);
    play_tab_.setOpaque(false);

    play_tab_.addAndMakeVisible(simulation_);
    play_tab_.addAndMakeVisible(waveform_);

    addAndMakeVisible(tap_btn_);
    addAndMakeVisible(snap_btn_);
    addAndMakeVisible(bpm_label_);

    mode_label_.setText("Mode", juce::dontSendNotification);
    mode_box_.addItem("Live Insert", 1);
    mode_box_.addItem("File Granular", 2);
    addAndMakeVisible(mode_label_);
    addAndMakeVisible(mode_box_);

    addAndMakeVisible(load_btn_);
    addAndMakeVisible(play_btn_);
    addAndMakeVisible(stop_btn_);
    addAndMakeVisible(export_btn_);
    addAndMakeVisible(seg_prev_btn_);
    addAndMakeVisible(seg_next_btn_);
    addAndMakeVisible(zoom_in_btn_);
    addAndMakeVisible(zoom_out_btn_);
    addAndMakeVisible(zoom_sel_btn_);
    addAndMakeVisible(zoom_fit_btn_);
    addAndMakeVisible(seq_btn_);
    addAndMakeVisible(pattern_btn_);
    addAndMakeVisible(expand_btn_);
    addAndMakeVisible(file_label_);

    bpm_label_.setFont(juce::FontOptions(11.0f));
    bpm_label_.setJustificationType(juce::Justification::centredLeft);

    seq_btn_.setTooltip("Seq: avance automatico de ventana al terminar segmento");
    pattern_btn_.setTooltip("Pat: saltar pasos desactivados en la grilla");
    seq_btn_.setClickingTogglesState(true);
    pattern_btn_.setClickingTogglesState(true);
    for (auto* tb : { &seq_btn_, &pattern_btn_ }) {
        tb->setColour(juce::ToggleButton::textColourId, juce::Colours::white);
        tb->setColour(juce::ToggleButton::tickColourId, juce::Colours::orange);
        tb->setColour(juce::ToggleButton::tickDisabledColourId,
                      juce::Colours::grey.withAlpha(0.6f));
    }

    expand_btn_.onClick = [this] { toggleExpanded(); };

    load_btn_.onClick = [this] { loadFileClicked(); };
    play_btn_.onClick = [this] {
        if (processor_.isFileGranularMode() && processor_.file_source().hasFile())
            processor_.setFilePlaying(true);
    };
    stop_btn_.onClick = [this] { processor_.setFilePlaying(false); };
    export_btn_.onClick = [this] {
        if (!processor_.isFileGranularMode()) return;
        auto chooser = std::make_shared<juce::FileChooser>(
            "Export WAV", juce::File(), "*.wav");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode,
            [this, chooser](const juce::FileChooser& fc) {
                auto f = fc.getResult();
                if (f == juce::File()) return;
                juce::String err;
                if (!processor_.renderSegmentToFile(f, err))
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon, "Export failed", err);
            });
    };

    seg_prev_btn_.onClick = [this] {
        processor_.shiftFileSegmentByWindows(-1);
        waveform_.refresh();
    };
    seg_next_btn_.onClick = [this] {
        processor_.shiftFileSegmentByWindows(1);
        waveform_.refresh();
    };
    zoom_in_btn_.onClick = [this] { waveform_.zoomIn(); waveform_.refresh(); };
    zoom_out_btn_.onClick = [this] { waveform_.zoomOut(); waveform_.refresh(); };
    zoom_sel_btn_.onClick = [this] { waveform_.zoomToSelection(); waveform_.refresh(); };
    zoom_fit_btn_.onClick = [this] { waveform_.resetView(); waveform_.refresh(); };

    tap_btn_.onClick = [this] {
        processor_.registerTapTempo();
        bpm_label_.setText(
            juce::String(processor_.get_apvts()
                .getRawParameterValue(ParamID::Bpm)->load(), 1) + " BPM",
            juce::dontSendNotification);
    };
    snap_btn_.onClick = [this] {
        processor_.snapSegmentQuarterNote();
        waveform_.refresh();
    };

    quality_label_.setText("FFT", juce::dontSendNotification);
    quality_box_.addItem("2048", 1);
    quality_box_.addItem("4096", 2);
    quality_label_.setOpaque(false);
    quality_label_.setColour(juce::Label::backgroundColourId,
                             juce::Colours::transparentBlack);
    quality_label_.setColour(juce::Label::textColourId,
                             juce::Colours::white.withAlpha(0.88f));
    play_tab_.addAndMakeVisible(quality_label_);
    play_tab_.addAndMakeVisible(quality_box_);
    quality_box_.setOpaque(false);
    quality_box_.setColour(juce::ComboBox::backgroundColourId,
                           juce::Colours::black.withAlpha(0.25f));
    quality_box_.setColour(juce::ComboBox::textColourId,
                           juce::Colours::white.withAlpha(0.9f));
    quality_box_.setColour(juce::ComboBox::outlineColourId,
                           juce::Colours::white.withAlpha(0.2f));

    auto add = [&](SliderWithLabel& s, const juce::String& name) {
        s.setup(name);
        play_tab_.addAndMakeVisible(s.slider);
        play_tab_.addAndMakeVisible(s.label);
    };
    add(coherence_,      "Coherence/Chaos");
    add(density_,        "Density");
    add(harmonic_depth_, "Harmonic Depth");
    add(pitch_shift_,    "Pitch Shift");
    pitch_shift_.slider.setTextValueSuffix(" st");
    add(tonal_residual_, "Tonal/Residual");
    add(gravity_,        "Gravity");
    add(motion_,         "Motion");
    add(decay_,          "Decay");
    add(spread_,         "Spread");
    add(input_gain_,     "Input Gain");
    input_gain_.slider.setTextValueSuffix(" dB");
    add(dry_wet_,        "Dry/Wet");
    add(output_gain_,    "Output Gain");
    output_gain_.slider.setTextValueSuffix(" dB");
    add(fragment_ms_,    "Fragment ms");
    add(temporal_scatter_, "Time Scatter");
    add(grain_voices_,   "Grains");
    add(bin_scatter_,    "Bin Scatter");
    add(seed_,           "Seed");
    add(pitch_spread_min_, "Spread Min");
    pitch_spread_min_.slider.setTextValueSuffix(" st");
    add(pitch_spread_max_, "Spread Max");
    pitch_spread_max_.slider.setTextValueSuffix(" st");
    add(window_xfade_,   "Win Xfade");
    window_xfade_.slider.setTextValueSuffix(" ms");
    add(bpm_,            "BPM");
    add(seg_start_,      "Seg Start");
    add(seg_end_,        "Seg End");

    auto& apvts = processor_.get_apvts();
    mode_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, ParamID::ProcessMode, mode_box_);
    coherence_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::CoherenceChaos, coherence_.slider);
    density_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::Density, density_.slider);
    harmonic_depth_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::HarmonicDepth, harmonic_depth_.slider);
    pitch_shift_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::PitchShift, pitch_shift_.slider);
    tonal_residual_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::TonalResidual, tonal_residual_.slider);
    gravity_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::Gravity, gravity_.slider);
    motion_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::Motion, motion_.slider);
    decay_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::Decay, decay_.slider);
    spread_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::Spread, spread_.slider);
    input_gain_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::InputGain, input_gain_.slider);
    dry_wet_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::DryWet, dry_wet_.slider);
    output_gain_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::OutputGain, output_gain_.slider);
    fragment_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::TemporalFragmentMs, fragment_ms_.slider);
    temporal_scatter_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::TemporalScramble, temporal_scatter_.slider);
    grain_voices_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::GrainVoices, grain_voices_.slider);
    scatter_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::BinScatter, bin_scatter_.slider);
    seed_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::RandomSeed, seed_.slider);
    seg_start_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::SegmentStart, seg_start_.slider);
    seg_end_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::SegmentEnd, seg_end_.slider);
    quality_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, ParamID::SpectralQuality, quality_box_);
    seq_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, ParamID::WindowSequencer, seq_btn_);
    pattern_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, ParamID::PatternEnabled, pattern_btn_);
    pitch_spread_min_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::PitchSpreadMin, pitch_spread_min_.slider);
    pitch_spread_max_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::PitchSpreadMax, pitch_spread_max_.slider);
    window_xfade_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::WindowXfadeMs, window_xfade_.slider);
    bpm_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::Bpm, bpm_.slider);

    mode_box_.onChange = [this] { updateModeVisibility(); };
    simulation_.toBack();
    updateModeVisibility();
}

void SpectraMorphAudioProcessorEditor::toggleExpanded() {
    if (auto* dw = findParentComponentOfClass<juce::DocumentWindow>()) {
        dw->setFullScreen(!dw->isFullScreen());
        expand_btn_.setButtonText(dw->isFullScreen() ? "Restore" : "Full");
        return;
    }

    if (expanded_) {
        setSize(default_size_.x, default_size_.y);
        expand_btn_.setButtonText("Expand");
        expanded_ = false;
        return;
    }

    default_size_ = { getWidth(), getHeight() };
    auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    const auto screen = display != nullptr ? display->userArea
                                           : juce::Rectangle<int>(0, 0, 1600, 1000);
    const int w = juce::jmin(1680, screen.getWidth() - 32);
    const int h = juce::jmin(1050, screen.getHeight() - 32);
    setSize(w, h);
    expand_btn_.setButtonText("Restore");
    expanded_ = true;
}

void SpectraMorphAudioProcessorEditor::layoutPlayKnobs(juce::Rectangle<int> slider_area) {
    slider_area = slider_area.reduced(5, 0);
    const bool file = processor_.isFileGranularMode();

    juce::Array<juce::Component*> knobs;
    knobs.add(&coherence_.slider);
    knobs.add(&density_.slider);
    knobs.add(&harmonic_depth_.slider);
    knobs.add(&pitch_shift_.slider);
    knobs.add(&tonal_residual_.slider);
    if (file) {
        knobs.add(&fragment_ms_.slider);
        knobs.add(&temporal_scatter_.slider);
        knobs.add(&grain_voices_.slider);
        knobs.add(&bin_scatter_.slider);
        knobs.add(&seed_.slider);
        knobs.add(&pitch_spread_min_.slider);
        knobs.add(&pitch_spread_max_.slider);
        knobs.add(&window_xfade_.slider);
        knobs.add(&bpm_.slider);
    } else {
        knobs.add(&gravity_.slider);
        knobs.add(&motion_.slider);
        knobs.add(&decay_.slider);
    }
    knobs.add(&spread_.slider);
    knobs.add(&input_gain_.slider);
    knobs.add(&dry_wet_.slider);
    knobs.add(&output_gain_.slider);

    const int n_slots = knobs.size() + (file ? 1 : 0);
    const int sw = slider_area.getWidth() / std::max(1, n_slots);

    for (auto* k : knobs) {
        if (k->isVisible())
            k->setBounds(slider_area.removeFromLeft(sw));
    }
    if (file && quality_box_.isVisible())
        quality_box_.setBounds(slider_area.removeFromLeft(sw).reduced(4, 20));
}

SpectraMorphAudioProcessorEditor::~SpectraMorphAudioProcessorEditor() {
    stopTimer();
    processor_.setFilePlaying(false);
}

void SpectraMorphAudioProcessorEditor::updateModeVisibility() {
    const bool file = processor_.isFileGranularMode();
    load_btn_.setVisible(file);
    play_btn_.setVisible(file);
    stop_btn_.setVisible(file);
    export_btn_.setVisible(file);
    seg_prev_btn_.setVisible(file);
    seg_next_btn_.setVisible(file);
    zoom_in_btn_.setVisible(file);
    zoom_out_btn_.setVisible(file);
    zoom_sel_btn_.setVisible(file);
    zoom_fit_btn_.setVisible(file);
    seq_btn_.setVisible(file);
    pattern_btn_.setVisible(file);
    tap_btn_.setVisible(file);
    snap_btn_.setVisible(file);
    bpm_label_.setVisible(file);
    waveform_.setVisible(file);
    file_label_.setVisible(file);
    fragment_ms_.slider.setVisible(file);
    fragment_ms_.label.setVisible(file);
    temporal_scatter_.slider.setVisible(file);
    temporal_scatter_.label.setVisible(file);
    grain_voices_.slider.setVisible(file);
    grain_voices_.label.setVisible(file);
    bin_scatter_.slider.setVisible(file);
    bin_scatter_.label.setVisible(file);
    seed_.slider.setVisible(file);
    seed_.label.setVisible(file);
    pitch_spread_min_.slider.setVisible(file);
    pitch_spread_min_.label.setVisible(file);
    pitch_spread_max_.slider.setVisible(file);
    pitch_spread_max_.label.setVisible(file);
    window_xfade_.slider.setVisible(file);
    window_xfade_.label.setVisible(file);
    bpm_.slider.setVisible(file);
    bpm_.label.setVisible(file);
    seg_start_.slider.setVisible(false);
    seg_start_.label.setVisible(false);
    seg_end_.slider.setVisible(false);
    seg_end_.label.setVisible(false);
    quality_box_.setVisible(file);
    quality_label_.setVisible(file);

    gravity_.slider.setVisible(!file);
    gravity_.label.setVisible(!file);
    motion_.slider.setVisible(!file);
    motion_.label.setVisible(!file);
    decay_.slider.setVisible(!file);
    decay_.label.setVisible(!file);

    coherence_.slider.setVisible(true);
    coherence_.label.setVisible(true);
    density_.slider.setVisible(true);
    density_.label.setVisible(true);
    harmonic_depth_.slider.setVisible(true);
    harmonic_depth_.label.setVisible(true);
    pitch_shift_.slider.setVisible(true);
    pitch_shift_.label.setVisible(true);
    tonal_residual_.slider.setVisible(true);
    tonal_residual_.label.setVisible(true);
    spread_.slider.setVisible(true);
    spread_.label.setVisible(true);
    input_gain_.slider.setVisible(true);
    input_gain_.label.setVisible(true);
    dry_wet_.slider.setVisible(true);
    dry_wet_.label.setVisible(true);
    output_gain_.slider.setVisible(true);
    output_gain_.label.setVisible(true);

    if (file && processor_.file_source().hasFile()) {
        const auto& src = processor_.file_source();
        file_label_.setText(
            src.fileName() + " | seg "
            + juce::String(src.segmentLengthSamples()) + " samples",
            juce::dontSendNotification);
    } else if (file) {
        file_label_.setText("No file loaded", juce::dontSendNotification);
    }

    layoutComponents();
}

void SpectraMorphAudioProcessorEditor::loadFileClicked() {
    auto chooser = std::make_shared<juce::FileChooser>(
        "Load audio", juce::File(),
        "*.wav;*.aiff;*.aif;*.flac;*.ogg");
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                         | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser](const juce::FileChooser& fc) {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            juce::String err;
            if (!processor_.loadSourceFile(f, err)) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, "Load failed", err);
                return;
            }
            waveform_.resetView();
            waveform_.refresh();
            updateModeVisibility();
        });
}

void SpectraMorphAudioProcessorEditor::layoutComponents() {
    constexpr int kMargin      = 10;
    constexpr int kTopBarH     = 28;
    constexpr int kTopBar2H    = 26;

    auto area = getLocalBounds().reduced(kMargin);

    auto top = area.removeFromTop(kTopBarH);
    mode_label_.setBounds(top.removeFromLeft(40));
    mode_box_.setBounds(top.removeFromLeft(140));
    top.removeFromLeft(8);
    load_btn_.setBounds(top.removeFromLeft(80));
    play_btn_.setBounds(top.removeFromLeft(50));
    stop_btn_.setBounds(top.removeFromLeft(50));
    export_btn_.setBounds(top.removeFromLeft(90));
    top.removeFromLeft(6);
    seg_prev_btn_.setBounds(top.removeFromLeft(52));
    top.removeFromLeft(4);
    seg_next_btn_.setBounds(top.removeFromLeft(52));
    top.removeFromLeft(6);
    zoom_in_btn_.setBounds(top.removeFromLeft(52));
    top.removeFromLeft(3);
    zoom_out_btn_.setBounds(top.removeFromLeft(52));
    top.removeFromLeft(3);
    zoom_sel_btn_.setBounds(top.removeFromLeft(58));
    top.removeFromLeft(3);
    zoom_fit_btn_.setBounds(top.removeFromLeft(36));
    top.removeFromLeft(8);
    expand_btn_.setBounds(top.removeFromRight(64));
    top.removeFromRight(6);
    file_label_.setBounds(top);

    const bool file = processor_.isFileGranularMode();
    if (file) {
        auto top2 = area.removeFromTop(kTopBar2H);
        seq_btn_.setBounds(top2.removeFromLeft(52).reduced(1));
        top2.removeFromLeft(4);
        pattern_btn_.setBounds(top2.removeFromLeft(52).reduced(1));
        top2.removeFromLeft(10);
        tap_btn_.setBounds(top2.removeFromLeft(44).reduced(1));
        snap_btn_.setBounds(top2.removeFromLeft(64).reduced(1));
        bpm_label_.setBounds(top2.reduced(2));
    } else {
        seq_btn_.setBounds({});
        pattern_btn_.setBounds({});
        tap_btn_.setBounds({});
        snap_btn_.setBounds({});
        bpm_label_.setBounds({});
    }

    play_tab_.setBounds(area);
    layoutPlayTab();
}

void SpectraMorphAudioProcessorEditor::layoutPlayTab() {
    constexpr int kVersionH     = 18;
    constexpr int kTelemetryH   = 32;
    constexpr int kKnobRowH     = 118;
    constexpr int kMinControlsH = kKnobRowH + 24;

    auto area = play_tab_.getLocalBounds().reduced(0, 4);
    version_bounds_   = area.removeFromTop(kVersionH);
    telemetry_bounds_ = area.removeFromTop(kTelemetryH).reduced(0, 2);

    const bool file = processor_.isFileGranularMode();
    if (file && area.getHeight() > kMinControlsH + 80) {
        const int wave_h = juce::jlimit(120, 280,
            area.getHeight() - kMinControlsH);
        waveform_bounds_ = area.removeFromTop(wave_h).reduced(0, 4);
        waveform_.setBounds(waveform_bounds_);
    } else {
        waveform_bounds_ = {};
        waveform_.setBounds({});
    }

    viz_bounds_ = area.reduced(0, 2);
    simulation_.setBounds(viz_bounds_);
    simulation_.toBack();

    auto knob_row = viz_bounds_;
    if (knob_row.getHeight() > kKnobRowH)
        knob_row = knob_row.removeFromBottom(kKnobRowH);
    layoutPlayKnobs(knob_row);
}

void SpectraMorphAudioProcessorEditor::resized() {
    layoutComponents();
}

void SpectraMorphAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(
        juce::ResizableWindow::backgroundColourId));

    const auto version_area = getLocalArea(&play_tab_, version_bounds_).reduced(4, 0);
    g.setFont(juce::FontOptions(13.0f).withStyle("Bold"));
    g.setColour(juce::Colours::white.withAlpha(0.62f));
    g.drawText("4ssiiz", version_area, juce::Justification::centredLeft);

    paint_version_badge(g, version_area);

    VisualState vs;
    const bool has_data = processor_.read_visual(vs);
    const auto telem = getLocalArea(&play_tab_, telemetry_bounds_);
    if (!telem.isEmpty()) {
        g.setFont(juce::FontOptions(11.0f));
        g.setColour(juce::Colours::lightgrey.withAlpha(0.85f));
        juce::String mode_str = processor_.isFileGranularMode()
            ? "FileGranular" : "Live";
        g.drawText(mode_str
            + "  f0: " + juce::String(processor_.telemetry_f0(), 0) + " Hz"
            + "  partials: " + juce::String(has_data ? vs.num_partials : 0u)
            + "  frames: " + juce::String(processor_.telemetry_frame_store())
            + "  |  Pat+Seq = pattern loop",
            telem, juce::Justification::centredLeft);
    }
}

void SpectraMorphAudioProcessorEditor::timerCallback() {
    if (waveform_.isVisible())
        waveform_.refresh();
    if (bpm_label_.isVisible()) {
        bpm_label_.setText(
            juce::String(processor_.get_apvts()
                .getRawParameterValue(ParamID::Bpm)->load(), 1) + " BPM",
            juce::dontSendNotification);
    }
    repaint();
}
