#include "PluginEditor.h"
#include "PluginProcessor.h"

SpectraMorphAudioProcessorEditor::SpectraMorphAudioProcessorEditor(
    SpectraMorphAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , processor_(p)
    , coherence_attach_(
          p.get_apvts(), ParamID::CoherenceChaos, coherence_slider_)
    , density_attach_(
          p.get_apvts(), ParamID::Density, density_slider_)
    , tonal_residual_attach_(
          p.get_apvts(), ParamID::TonalResidual, tonal_residual_slider_)
    , gravity_attach_(
          p.get_apvts(), ParamID::Gravity, gravity_slider_)
    , motion_attach_(
          p.get_apvts(), ParamID::Motion, motion_slider_)
    , decay_attach_(
          p.get_apvts(), ParamID::Decay, decay_slider_)
    , spread_attach_(
          p.get_apvts(), ParamID::Spread, spread_slider_)
    , dry_wet_attach_(
          p.get_apvts(), ParamID::DryWet, dry_wet_slider_)
{
    setSize(800, 500);
    startTimerHz(30);

    auto setup_slider = [](juce::Slider& s) {
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    };

    setup_slider(coherence_slider_);
    setup_slider(density_slider_);
    setup_slider(tonal_residual_slider_);
    setup_slider(gravity_slider_);
    setup_slider(motion_slider_);
    setup_slider(decay_slider_);
    setup_slider(spread_slider_);
    setup_slider(dry_wet_slider_);

    addAndMakeVisible(coherence_slider_);
    addAndMakeVisible(density_slider_);
    addAndMakeVisible(tonal_residual_slider_);
    addAndMakeVisible(gravity_slider_);
    addAndMakeVisible(motion_slider_);
    addAndMakeVisible(decay_slider_);
    addAndMakeVisible(spread_slider_);
    addAndMakeVisible(dry_wet_slider_);
}

SpectraMorphAudioProcessorEditor::~SpectraMorphAudioProcessorEditor() {
    stopTimer();
}

void SpectraMorphAudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(20);
    int w = area.getWidth() / 8;

    coherence_slider_.setBounds(area.removeFromLeft(w));
    density_slider_.setBounds(area.removeFromLeft(w));
    tonal_residual_slider_.setBounds(area.removeFromLeft(w));
    gravity_slider_.setBounds(area.removeFromLeft(w));
    motion_slider_.setBounds(area.removeFromLeft(w));
    decay_slider_.setBounds(area.removeFromLeft(w));
    spread_slider_.setBounds(area.removeFromLeft(w));
    dry_wet_slider_.setBounds(area.removeFromLeft(w));
}

void SpectraMorphAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(
        juce::ResizableWindow::backgroundColourId));

    g.setColour(juce::Colours::white);
    g.setFont(14.0f);

    // Draw particle visualization
    VisualState vs;
    if (processor_.read_visual(vs) && vs.num_partials > 0) {
        auto area = getLocalBounds().toFloat().reduced(10, 100);
        float h = area.getHeight();
        float w = area.getWidth();

        for (uint32_t i = 0; i < vs.num_partials; ++i) {
            auto& p = vs.partials[i];
            if (p.amplitude < 0.01f) continue;

            float x = area.getX() + (p.spectral_pos / LOG_OCTAVES) * w;
            float y = area.getY() + h * 0.5f;
            float r = 2.0f + p.amplitude * 4.0f;

            // Color by harmonic affinity
            float hue = 0.6f - p.harmonic_affinity * 0.5f;  // blue → green
            float alpha = p.coherence * 0.8f + 0.2f;

            g.setColour(juce::Colour::fromHSV(hue, 0.8f, 0.8f, alpha));
            g.fillEllipse(x - r, y - r, r * 2, r * 2);
        }

        // Telemetry text
        g.setColour(juce::Colours::grey);
        g.drawText("Partials: " + juce::String(vs.num_partials)
            + "  Coherence: " + juce::String(vs.global_coherence, 2)
            + "  CPU: " + juce::String(vs.cpu_load * 100.0f, 1) + "%",
            10, getHeight() - 30, getWidth() - 20, 20,
            juce::Justification::centredLeft);
    }
}

void SpectraMorphAudioProcessorEditor::timerCallback() {
    repaint();
}
