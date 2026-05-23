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

    // Sliders for macro controls
    juce::Slider coherence_slider_;
    juce::Slider density_slider_;
    juce::Slider tonal_residual_slider_;
    juce::Slider gravity_slider_;
    juce::Slider motion_slider_;
    juce::Slider decay_slider_;
    juce::Slider spread_slider_;
    juce::Slider dry_wet_slider_;

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
