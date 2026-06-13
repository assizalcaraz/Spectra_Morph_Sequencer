#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class SpectraMorphAudioProcessor;

class WaveformSegmentView;

class PatternGridView final : public juce::Component {
public:
    PatternGridView(SpectraMorphAudioProcessor& p, WaveformSegmentView& wave);

    void refresh();
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void rebuildIfNeeded();
    void layoutTimelineSteps();

    SpectraMorphAudioProcessor& processor_;
    WaveformSegmentView& waveform_;
    std::vector<std::unique_ptr<juce::TextButton>> step_buttons_;
    uint32_t last_step_count_ = 0;
    uint32_t last_mask_ = 0;
    int last_current_ = -1;
    float last_view_start_ = -1.0f;
    float last_view_end_ = -1.0f;
    int last_width_ = -1;
};

class WaveformSegmentView final : public juce::Component {
public:
    explicit WaveformSegmentView(SpectraMorphAudioProcessor& p);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void refresh();
    void resetView();
    void zoomIn();
    void zoomOut();
    void zoomToSelection();

    float fileNormToX(float file_norm) const;
    float viewStartNorm() const { return view_start_; }
    float viewEndNorm() const { return view_end_; }

private:
    float viewNormFromX(int x) const;
    float fileNormFromX(int x) const;
    float xFromFileNorm(float file_norm) const;
    void setViewRange(float start, float end);
    void applyMarqueeSelection();
    void paintStepGrid(juce::Graphics& g, juce::Rectangle<float> area, float view_span);

    SpectraMorphAudioProcessor& processor_;
    std::unique_ptr<PatternGridView> pattern_grid_;
    enum class DragMode { None, Move, ResizeStart, ResizeEnd, Marquee };
    DragMode drag_ = DragMode::None;
    float drag_anchor_norm_ = 0.0f;
    float seg0_at_down_ = 0.0f;
    float seg1_at_down_ = 0.0f;
    float marquee_start_ = 0.0f;
    float marquee_end_ = 0.0f;
    float view_start_ = 0.0f;
    float view_end_ = 1.0f;
};

class SimulationView final : public juce::Component, public juce::Timer {
public:
    explicit SimulationView(SpectraMorphAudioProcessor& p);
    ~SimulationView() override { stopTimer(); }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override { repaint(); }

private:
    SpectraMorphAudioProcessor& processor_;
};

class SpectraMorphAudioProcessorEditor final
    : public juce::AudioProcessorEditor
    , public juce::Timer
{
public:
    explicit SpectraMorphAudioProcessorEditor(SpectraMorphAudioProcessor&);
    ~SpectraMorphAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    void updateModeVisibility();
    void loadFileClicked();
    void layoutComponents();
    void layoutPlayTab();
    void layoutPlayKnobs(juce::Rectangle<int> slider_area);
    void toggleExpanded();

    SpectraMorphAudioProcessor& processor_;

    juce::Component play_tab_;
    SimulationView simulation_;
    WaveformSegmentView waveform_;
    juce::TextButton expand_btn_ { "Expand" };

    bool expanded_ = false;
    juce::Point<int> default_size_ { 1024, 780 };

    juce::Rectangle<int> version_bounds_;
    juce::Rectangle<int> telemetry_bounds_;
    juce::Rectangle<int> waveform_bounds_;
    juce::Rectangle<int> viz_bounds_;

    juce::ComboBox mode_box_;
    juce::Label mode_label_;
    juce::TextButton load_btn_ { "Load WAV" };
    juce::TextButton play_btn_ { "Play" };
    juce::TextButton stop_btn_ { "Stop" };
    juce::TextButton export_btn_ { "Export WAV" };
    juce::TextButton seg_prev_btn_ { "◀ Win" };
    juce::TextButton seg_next_btn_ { "Win ▶" };
    juce::TextButton zoom_in_btn_  { "Zoom +" };
    juce::TextButton zoom_out_btn_ { "Zoom −" };
    juce::TextButton zoom_sel_btn_ { "Zoom Sel" };
    juce::TextButton zoom_fit_btn_ { "Fit" };
    juce::ToggleButton seq_btn_ { "Seq" };
    juce::ToggleButton pattern_btn_ { "Pat" };
    juce::TextButton tap_btn_ { "Tap" };
    juce::TextButton snap_btn_ { "Snap 1/4" };
    juce::Label file_label_;
    juce::Label bpm_label_;

    struct SliderWithLabel {
        juce::Slider slider;
        juce::Label  label;
        void setup(const juce::String& name) {
            slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
            slider.setOpaque(false);
            slider.setColour(juce::Slider::backgroundColourId,
                             juce::Colours::black.withAlpha(0.28f));
            slider.setColour(juce::Slider::rotarySliderFillColourId,
                             juce::Colours::orange.withAlpha(0.82f));
            slider.setColour(juce::Slider::rotarySliderOutlineColourId,
                             juce::Colours::white.withAlpha(0.38f));
            slider.setColour(juce::Slider::thumbColourId,
                             juce::Colours::orange.withAlpha(0.92f));
            slider.setColour(juce::Slider::textBoxTextColourId,
                             juce::Colours::white.withAlpha(0.95f));
            slider.setColour(juce::Slider::textBoxBackgroundColourId,
                             juce::Colours::black.withAlpha(0.52f));
            slider.setColour(juce::Slider::textBoxOutlineColourId,
                             juce::Colours::transparentBlack);
            label.setText(name, juce::dontSendNotification);
            label.setJustificationType(juce::Justification::centred);
            label.attachToComponent(&slider, false);
            label.setFont(12.0f);
            label.setOpaque(false);
            label.setColour(juce::Label::backgroundColourId,
                            juce::Colours::transparentBlack);
            label.setColour(juce::Label::textColourId,
                            juce::Colours::white.withAlpha(0.88f));
        }
    };

    SliderWithLabel coherence_;
    SliderWithLabel density_;
    SliderWithLabel harmonic_depth_;
    SliderWithLabel pitch_shift_;
    SliderWithLabel tonal_residual_;
    SliderWithLabel gravity_;
    SliderWithLabel motion_;
    SliderWithLabel decay_;
    SliderWithLabel spread_;
    SliderWithLabel input_gain_;
    SliderWithLabel dry_wet_;
    SliderWithLabel output_gain_;
    SliderWithLabel fragment_ms_;
    SliderWithLabel temporal_scatter_;
    SliderWithLabel grain_voices_;
    SliderWithLabel bin_scatter_;
    SliderWithLabel seed_;
    SliderWithLabel pitch_spread_min_;
    SliderWithLabel pitch_spread_max_;
    SliderWithLabel window_xfade_;
    SliderWithLabel bpm_;
    SliderWithLabel seg_start_;
    SliderWithLabel seg_end_;

    juce::ComboBox quality_box_;
    juce::Label quality_label_;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mode_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> coherence_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> density_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> harmonic_depth_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pitch_shift_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tonal_residual_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gravity_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> motion_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> decay_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> spread_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> input_gain_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dry_wet_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> output_gain_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> fragment_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> temporal_scatter_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> grain_voices_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> scatter_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> seed_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> seg_start_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> seg_end_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> quality_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> seq_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> pattern_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pitch_spread_min_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pitch_spread_max_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> window_xfade_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bpm_attach_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectraMorphAudioProcessorEditor)
};
