#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class SpectraMorphAudioProcessor;

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
    SpectraMorphAudioProcessor& processor_;

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

    juce::AudioProcessorValueTreeState::SliderAttachment coherence_attach_;
    juce::AudioProcessorValueTreeState::SliderAttachment density_attach_;
    juce::AudioProcessorValueTreeState::SliderAttachment tonal_residual_attach_;
    juce::AudioProcessorValueTreeState::SliderAttachment gravity_attach_;
    juce::AudioProcessorValueTreeState::SliderAttachment motion_attach_;
    juce::AudioProcessorValueTreeState::SliderAttachment decay_attach_;
    juce::AudioProcessorValueTreeState::SliderAttachment spread_attach_;
    juce::AudioProcessorValueTreeState::SliderAttachment dry_wet_attach_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectraMorphAudioProcessorEditor)
};
