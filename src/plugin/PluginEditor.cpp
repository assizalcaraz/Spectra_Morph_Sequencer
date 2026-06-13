#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "BuildVersion.h"
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

} // namespace

WaveformSegmentView::WaveformSegmentView(SpectraMorphAudioProcessor& p)
    : processor_(p)
{
}

void WaveformSegmentView::refresh() { repaint(); }

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

    g.setColour(juce::Colours::darkgrey);
    for (size_t i = 0; i < thumb.size(); ++i) {
        const float x = area.getX() + (static_cast<float>(i) / static_cast<float>(thumb.size())) * w;
        const float amp = thumb[i] * h * 0.45f;
        g.drawVerticalLine(static_cast<int>(x), mid - amp, mid + amp);
    }

    const float s0 = src.segmentStartNorm();
    const float s1 = src.segmentEndNorm();
    const float x0 = area.getX() + s0 * w;
    const float x1 = area.getX() + s1 * w;

    g.setColour(juce::Colours::orange.withAlpha(0.25f));
    g.fillRect(x0, area.getY(), x1 - x0, area.getHeight());

    g.setColour(juce::Colours::orange);
    g.drawVerticalLine(static_cast<int>(x0), area.getY(), area.getBottom());
    g.drawVerticalLine(static_cast<int>(x1), area.getY(), area.getBottom());
}

void WaveformSegmentView::resized() {}

void WaveformSegmentView::mouseDown(const juce::MouseEvent& e) {
    if (!processor_.file_source().hasFile()) return;
    const int w = getWidth();
    if (w <= 0) return;
    const float norm = static_cast<float>(e.x) / static_cast<float>(w);
    const float s0 = processor_.file_source().segmentStartNorm();
    const float s1 = processor_.file_source().segmentEndNorm();
    if (std::abs(norm - s0) < std::abs(norm - s1))
        drag_ = DragHandle::Start;
    else
        drag_ = DragHandle::End;
}

void WaveformSegmentView::mouseDrag(const juce::MouseEvent& e) {
    if (drag_ == DragHandle::None) return;
    const int w = getWidth();
    if (w <= 0) return;
    auto& apvts = processor_.get_apvts();
    float norm = std::clamp(static_cast<float>(e.x) / static_cast<float>(w), 0.0f, 1.0f);
    if (drag_ == DragHandle::Start) {
        if (auto* p = apvts.getParameter(ParamID::SegmentStart))
            p->setValueNotifyingHost(p->convertTo0to1(norm));
        const float end = apvts.getRawParameterValue(ParamID::SegmentEnd)->load();
        const float end_adj = std::min(1.0f, norm + 0.02f);
        if (norm >= end - 0.02f) {
            if (auto* pe = apvts.getParameter(ParamID::SegmentEnd))
                pe->setValueNotifyingHost(pe->convertTo0to1(end_adj));
        }
    } else {
        if (auto* p = apvts.getParameter(ParamID::SegmentEnd))
            p->setValueNotifyingHost(p->convertTo0to1(norm));
        const float start = apvts.getRawParameterValue(ParamID::SegmentStart)->load();
        const float start_adj = std::max(0.0f, norm - 0.02f);
        if (norm <= start + 0.02f) {
            if (auto* ps = apvts.getParameter(ParamID::SegmentStart))
                ps->setValueNotifyingHost(ps->convertTo0to1(start_adj));
        }
    }
    refresh();
}

SpectraMorphAudioProcessorEditor::SpectraMorphAudioProcessorEditor(
    SpectraMorphAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , processor_(p)
    , waveform_(p)
{
    setSize(900, 640);
    startTimerHz(30);

    mode_label_.setText("Mode", juce::dontSendNotification);
    mode_box_.addItem("Live Insert", 1);
    mode_box_.addItem("File Granular", 2);
    addAndMakeVisible(mode_label_);
    addAndMakeVisible(mode_box_);

    addAndMakeVisible(load_btn_);
    addAndMakeVisible(play_btn_);
    addAndMakeVisible(stop_btn_);
    addAndMakeVisible(export_btn_);
    addAndMakeVisible(file_label_);
    addAndMakeVisible(waveform_);

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

    quality_label_.setText("FFT", juce::dontSendNotification);
    quality_box_.addItem("2048", 1);
    quality_box_.addItem("4096", 2);
    addAndMakeVisible(quality_label_);
    addAndMakeVisible(quality_box_);

    auto add = [&](SliderWithLabel& s, const juce::String& name) {
        s.setup(name);
        addAndMakeVisible(s.slider);
    };
    add(coherence_,      "Coherence/Chaos");
    add(density_,        "Density");
    add(tonal_residual_, "Tonal/Residual");
    add(gravity_,        "Gravity");
    add(motion_,         "Motion");
    add(decay_,          "Decay");
    add(spread_,         "Spread");
    add(dry_wet_,        "Dry/Wet");
    add(fragment_ms_,    "Fragment ms");
    add(bin_scatter_,    "Bin Scatter");
    add(seed_,           "Seed");
    add(seg_start_,      "Seg Start");
    add(seg_end_,        "Seg End");

    auto& apvts = processor_.get_apvts();
    mode_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, ParamID::ProcessMode, mode_box_);
    coherence_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::CoherenceChaos, coherence_.slider);
    density_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::Density, density_.slider);
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
    dry_wet_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::DryWet, dry_wet_.slider);
    fragment_attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::TemporalFragmentMs, fragment_ms_.slider);
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

    mode_box_.onChange = [this] { updateModeVisibility(); };
    updateModeVisibility();
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
    file_label_.setVisible(file);
    waveform_.setVisible(file);
    fragment_ms_.slider.setVisible(file);
    bin_scatter_.slider.setVisible(file);
    seed_.slider.setVisible(file);
    seg_start_.slider.setVisible(file);
    seg_end_.slider.setVisible(file);
    quality_box_.setVisible(file);
    quality_label_.setVisible(file);

    gravity_.slider.setVisible(!file);
    motion_.slider.setVisible(!file);
    decay_.slider.setVisible(!file);
    spread_.slider.setVisible(true);
    dry_wet_.slider.setVisible(true);

    if (file && processor_.file_source().hasFile()) {
        const auto& src = processor_.file_source();
        file_label_.setText(
            src.fileName() + " | seg "
            + juce::String(src.segmentLengthSamples()) + " samples",
            juce::dontSendNotification);
    } else if (file) {
        file_label_.setText("No file loaded", juce::dontSendNotification);
    }
    resized();
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
            waveform_.refresh();
            updateModeVisibility();
        });
}

void SpectraMorphAudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(10);
    auto top = area.removeFromTop(28);
    mode_label_.setBounds(top.removeFromLeft(40));
    mode_box_.setBounds(top.removeFromLeft(140));
    top.removeFromLeft(8);
    load_btn_.setBounds(top.removeFromLeft(80));
    play_btn_.setBounds(top.removeFromLeft(50));
    stop_btn_.setBounds(top.removeFromLeft(50));
    export_btn_.setBounds(top.removeFromLeft(90));
    file_label_.setBounds(top);

    auto slider_area = area.removeFromBottom(120).reduced(5, 0);
    const bool file = processor_.isFileGranularMode();

    juce::Array<juce::Component*> knobs;
    knobs.add(&coherence_.slider);
    knobs.add(&density_.slider);
    knobs.add(&tonal_residual_.slider);
    if (file) {
        knobs.add(&fragment_ms_.slider);
        knobs.add(&bin_scatter_.slider);
        knobs.add(&seed_.slider);
        knobs.add(&seg_start_.slider);
        knobs.add(&seg_end_.slider);
    } else {
        knobs.add(&gravity_.slider);
        knobs.add(&motion_.slider);
        knobs.add(&decay_.slider);
    }
    knobs.add(&spread_.slider);
    knobs.add(&dry_wet_.slider);

    const int n_slots = knobs.size() + (file ? 1 : 0);
    const int sw = slider_area.getWidth() / std::max(1, n_slots);

    for (auto* k : knobs) {
        if (k->isVisible())
            k->setBounds(slider_area.removeFromLeft(sw));
    }
    if (file && quality_box_.isVisible())
        quality_box_.setBounds(slider_area.removeFromLeft(sw).reduced(4, 20));

    auto telemetry = area.removeFromTop(36).reduced(4, 0);
    waveform_.setBounds(area.reduced(0, 4));
    juce::ignoreUnused(telemetry);
}

void SpectraMorphAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(
        juce::ResizableWindow::backgroundColourId));

    auto bounds = getLocalBounds().reduced(12);
    bounds.removeFromTop(28);
    auto version_bar = bounds.removeFromTop(18);
    paint_version_badge(g, version_bar.reduced(4, 0));
    auto telemetry = bounds.removeFromTop(36).reduced(10, 8);
    bounds.removeFromBottom(120);

    VisualState vs;
    const bool has_data = processor_.read_visual(vs);

    if (has_data && vs.num_partials > 0) {
        auto viz = bounds.reduced(0, 4).toFloat();
        const float h = viz.getHeight();
        const float w = viz.getWidth();
        const float cy = viz.getCentreY();

        for (uint32_t i = 0; i < vs.num_partials; ++i) {
            auto& pt = vs.partials[i];
            if (pt.amplitude < 0.01f) continue;
            const float x = viz.getX() + (pt.spectral_pos / LOG_OCTAVES) * w;
            const float y = cy + (pt.frequency / 5000.0f - 0.5f) * h * 0.6f;
            const float r = 2.0f + pt.amplitude * 6.0f;
            const float hue = 0.6f - pt.harmonic_affinity * 0.5f;
            g.setColour(juce::Colour::fromHSV(hue, 0.8f, 0.8f, pt.coherence * 0.8f + 0.2f));
            g.fillEllipse(x - r, y - r, r * 2, r * 2);
        }

        g.setFont(juce::FontOptions(12.0f));
        g.setColour(juce::Colours::lightgrey);
        const float mode_coherence = 1.0f - processor_.get_apvts()
            .getRawParameterValue(ParamID::CoherenceChaos)->load();
        juce::String mode_str = processor_.isFileGranularMode()
            ? "FileGranular" : "Live";
        g.drawText("Mode: " + mode_str
            + "  Partials: " + juce::String(vs.num_partials)
            + "  f0: " + juce::String(processor_.telemetry_f0(), 0) + " Hz"
            + "  Coherence: " + juce::String(mode_coherence, 2)
            + "  ScrambleRd: " + juce::String(processor_.telemetry_scramble_read())
            + "  Frames: " + juce::String(processor_.telemetry_frame_store())
            + "  CPU: " + juce::String(vs.cpu_load * 100.0f, 1) + "%",
            telemetry, juce::Justification::centredLeft);
    } else {
        g.setColour(juce::Colours::darkgrey);
        g.setFont(juce::FontOptions(14.0f));
        g.drawText("SpectraMorph " + build_version_label(),
            bounds.toNearestInt(), juce::Justification::centred);
    }

    waveform_.refresh();
}

void SpectraMorphAudioProcessorEditor::timerCallback() {
    repaint();
}
