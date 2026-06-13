#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class SpectraMorphAudioProcessor;

class WaveformSegmentView final : public juce::Component {
public:
    explicit WaveformSegmentView(SpectraMorphAudioProcessor& p);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void refresh();

private:
    SpectraMorphAudioProcessor& processor_;
    enum class DragHandle { None, Start, End };
    DragHandle drag_ = DragHandle::None;
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

    SpectraMorphAudioProcessor& processor_;

    juce::ComboBox mode_box_;
    juce::Label mode_label_;
    juce::TextButton load_btn_ { "Load WAV" };
    juce::TextButton play_btn_ { "Play" };
    juce::TextButton stop_btn_ { "Stop" };
    juce::TextButton export_btn_ { "Export WAV" };
    juce::Label file_label_;

    WaveformSegmentView waveform_;

    struct SliderWithLabel {
        juce::Slider slider;
        juce::Label  label;
        void setup(const juce::String& name) {
            slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
            label.setText(name, juce::dontSendNotification);
            label.setJustificationType(juce::Justification::centred);
            label.attachToComponent(&slider, false);
            label.setFont(12.0f);
        }
    };

    SliderWithLabel coherence_;
    SliderWithLabel density_;
    SliderWithLabel tonal_residual_;
    SliderWithLabel gravity_;
    SliderWithLabel motion_;
    SliderWithLabel decay_;
    SliderWithLabel spread_;
    SliderWithLabel dry_wet_;
    SliderWithLabel fragment_ms_;
    SliderWithLabel bin_scatter_;
    SliderWithLabel seed_;
    SliderWithLabel seg_start_;
    SliderWithLabel seg_end_;

    juce::ComboBox quality_box_;
    juce::Label quality_label_;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mode_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> coherence_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> density_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tonal_residual_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gravity_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> motion_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> decay_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> spread_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dry_wet_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> fragment_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> scatter_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> seed_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> seg_start_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> seg_end_attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> quality_attach_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectraMorphAudioProcessorEditor)
};
