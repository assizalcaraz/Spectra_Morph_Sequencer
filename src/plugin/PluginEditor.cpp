#include "PluginEditor.h"
#include "PluginProcessor.h"

SpectraMorphAudioProcessorEditor::SpectraMorphAudioProcessorEditor(
    SpectraMorphAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , processor_(p)
    , coherence_attach_(
          p.get_apvts(), ParamID::CoherenceChaos, coherence_.slider)
    , density_attach_(
          p.get_apvts(), ParamID::Density, density_.slider)
    , tonal_residual_attach_(
          p.get_apvts(), ParamID::TonalResidual, tonal_residual_.slider)
    , gravity_attach_(
          p.get_apvts(), ParamID::Gravity, gravity_.slider)
    , motion_attach_(
          p.get_apvts(), ParamID::Motion, motion_.slider)
    , decay_attach_(
          p.get_apvts(), ParamID::Decay, decay_.slider)
    , spread_attach_(
          p.get_apvts(), ParamID::Spread, spread_.slider)
    , dry_wet_attach_(
          p.get_apvts(), ParamID::DryWet, dry_wet_.slider)
{
    setSize(800, 560);
    startTimerHz(30);

    auto add = [&](SliderWithLabel& s, const juce::String& name) {
        s.setup(name);
        addAndMakeVisible(s.slider);
    };
    add(coherence_,      "Coherence↔Chaos");
    add(density_,        "Density");
    add(tonal_residual_, "Tonal/Residual");
    add(gravity_,        "Gravity");
    add(motion_,         "Motion");
    add(decay_,          "Decay");
    add(spread_,         "Spread");
    add(dry_wet_,        "Dry/Wet");
}

SpectraMorphAudioProcessorEditor::~SpectraMorphAudioProcessorEditor() {
    stopTimer();
}

void SpectraMorphAudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(10);
    auto slider_area = area.removeFromBottom(120).reduced(5, 0);
    int sw = slider_area.getWidth() / 8;

    coherence_.slider.setBounds(slider_area.removeFromLeft(sw));
    density_.slider.setBounds(slider_area.removeFromLeft(sw));
    tonal_residual_.slider.setBounds(slider_area.removeFromLeft(sw));
    gravity_.slider.setBounds(slider_area.removeFromLeft(sw));
    motion_.slider.setBounds(slider_area.removeFromLeft(sw));
    decay_.slider.setBounds(slider_area.removeFromLeft(sw));
    spread_.slider.setBounds(slider_area.removeFromLeft(sw));
    dry_wet_.slider.setBounds(slider_area.removeFromLeft(sw));
}

void SpectraMorphAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(
        juce::ResizableWindow::backgroundColourId));

    auto bounds = getLocalBounds().reduced(12);
    bounds.removeFromBottom(120);  // slider strip reserved in resized()
    auto telemetry = bounds.removeFromTop(36).reduced(8, 10);
    auto viz_area = bounds.reduced(0, 4).toFloat();

    VisualState vs;
    bool has_data = processor_.read_visual(vs);

    if (has_data && vs.num_partials > 0) {
        float h = viz_area.getHeight();
        float w = viz_area.getWidth();
        float cy = viz_area.getCentreY();

        for (uint32_t i = 0; i < vs.num_partials; ++i) {
            auto& p = vs.partials[i];
            if (p.amplitude < 0.01f) continue;

            float x = viz_area.getX() + (p.spectral_pos / LOG_OCTAVES) * w;
            float y = cy + (p.frequency / 5000.0f - 0.5f) * h * 0.6f;
            float r = 2.0f + p.amplitude * 6.0f;
            float hue = 0.6f - p.harmonic_affinity * 0.5f;
            float alpha = p.coherence * 0.8f + 0.2f;

            g.setColour(juce::Colour::fromHSV(hue, 0.8f, 0.8f, alpha));
            g.fillEllipse(x - r, y - r, r * 2, r * 2);
        }

        g.setFont(juce::FontOptions(13.0f));
        g.setColour(juce::Colours::lightgrey);
        const float mode_coherence = 1.0f - processor_.get_apvts()
            .getRawParameterValue(ParamID::CoherenceChaos)->load();
        g.drawText("Partials: " + juce::String(vs.num_partials)
            + "  Mode: " + juce::String(mode_coherence, 2)
            + "  Phase lock: " + juce::String(vs.global_coherence, 2)
            + "  CPU: " + juce::String(vs.cpu_load * 100.0f, 1) + "%",
            telemetry,
            juce::Justification::centredLeft);
    } else {
        g.setColour(juce::Colours::darkgrey);
        g.setFont(juce::FontOptions(18.0f));
        g.drawText("SpectraMorph — insert effect: processa el audio del track en tiempo real",
            viz_area.toNearestInt(),
            juce::Justification::centred);

        g.setFont(juce::FontOptions(14.0f));
        g.drawText("Ajusta los parámetros para esculpir la materia espectral",
            viz_area.getX(), viz_area.getCentreY() + 30.0f,
            viz_area.getWidth(), 20.0f,
            juce::Justification::centred);
    }
}

void SpectraMorphAudioProcessorEditor::timerCallback() {
    repaint();
}
