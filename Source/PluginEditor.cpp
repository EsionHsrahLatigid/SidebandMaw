#include "PluginEditor.h"
#include "ParameterIDs.h"

namespace
{
namespace design = ehl::juce_design;
}

SidebandMawAudioProcessorEditor::SidebandMawAudioProcessorEditor(SidebandMawAudioProcessor& owner)
    : AudioProcessorEditor(owner), ownerProcessor(owner)
{
    setLookAndFeel(&lookAndFeel);
    setName("SidebandMaw editor");
    setComponentID("sidebandmaw-editor");
    setTitle("SidebandMaw");
    setDescription("SidebandMaw monochrome 8-bit sideband modulation editor");
    setWantsKeyboardFocus(true);

    meter.setComponentID("sidebandmaw-sideband-meter");
    addAndMakeVisible(meter);

    design::styleLabel(status);
    status.setComponentID("sidebandmaw-status");
    status.setJustificationType(juce::Justification::centredLeft);
    status.setColour(juce::Label::textColourId, design::Palette::mid());
    addAndMakeVisible(status);

    const juce::StringArray sliderNames { "SHIFT", "FDBK", "SPREAD", "DRIVE", "TONE", "MIX", "OUT" };
    for (std::size_t i = 0; i < sliders.size(); ++i)
        configureControl(sliders[i], labels[i], sliderNames[static_cast<int>(i)]);

    design::styleLabel(labels.back());
    labels.back().setText("MODE", juce::dontSendNotification);
    labels.back().setJustificationType(juce::Justification::centred);
    addAndMakeVisible(labels.back());

    modeBox.setComponentID("sidebandmaw-mode");
    modeBox.addItem("SHIFT", 1);
    modeBox.addItem("RING", 2);
    modeBox.addItem("MAW", 3);
    modeBox.setColour(juce::ComboBox::backgroundColourId, design::Palette::ink());
    modeBox.setColour(juce::ComboBox::textColourId, design::Palette::paper());
    modeBox.setColour(juce::ComboBox::outlineColourId, design::Palette::mid());
    modeBox.setColour(juce::ComboBox::arrowColourId, design::Palette::paper());
    addAndMakeVisible(modeBox);

    sliderAttachments[0] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        ownerProcessor.parameters, sidebandmaw::parameters::shift, sliders[0]);
    sliderAttachments[1] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        ownerProcessor.parameters, sidebandmaw::parameters::feedback, sliders[1]);
    sliderAttachments[2] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        ownerProcessor.parameters, sidebandmaw::parameters::spread, sliders[2]);
    sliderAttachments[3] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        ownerProcessor.parameters, sidebandmaw::parameters::drive, sliders[3]);
    sliderAttachments[4] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        ownerProcessor.parameters, sidebandmaw::parameters::tone, sliders[4]);
    sliderAttachments[5] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        ownerProcessor.parameters, sidebandmaw::parameters::mix, sliders[5]);
    sliderAttachments[6] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        ownerProcessor.parameters, sidebandmaw::parameters::output, sliders[6]);
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        ownerProcessor.parameters, sidebandmaw::parameters::mode, modeBox);

    setResizable(false, false);
    setSize(defaultWidth, defaultHeight);
    startTimerHz(24);
    updateReadout();
}

SidebandMawAudioProcessorEditor::~SidebandMawAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void SidebandMawAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    design::paintEditorChrome(graphics, getLocalBounds(), "SidebandMaw", "SSB / RING / MAW");

    auto bounds = getLocalBounds().withTrimmedTop(64).reduced(design::Metrics::margin, 0);
    auto meterBounds = bounds.removeFromTop(112);
    graphics.setColour(design::Palette::low());
    graphics.fillRect(meterBounds);
    graphics.setColour(design::Palette::mid());
    graphics.drawRect(meterBounds, 1);

    auto statusBounds = bounds.removeFromTop(24).withTrimmedTop(8);
    graphics.setColour(design::Palette::ink());
    graphics.fillRect(statusBounds);
    graphics.setColour(design::Palette::mid());
    graphics.drawRect(statusBounds, 1);

    auto controls = bounds.withTrimmedTop(8).withTrimmedBottom(design::Metrics::margin);
    graphics.setColour(design::Palette::low());
    graphics.drawRect(controls, 1);
}

void SidebandMawAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().withTrimmedTop(64).reduced(design::Metrics::margin, 0);
    meter.setBounds(bounds.removeFromTop(112).reduced(8));
    status.setBounds(bounds.removeFromTop(24).withTrimmedTop(8).reduced(8, 0));
    auto controls = bounds.withTrimmedTop(8).withTrimmedBottom(design::Metrics::margin);

    const auto gap = 4;
    const auto labelH = 14;
    const auto rowH = controls.getHeight() / 2;
    auto top = controls.removeFromTop(rowH).reduced(8, 4);
    auto bottom = controls.reduced(8, 4);

    const auto topW = top.getWidth() / 4;
    for (int i = 0; i < 3; ++i)
    {
        auto cell = top.removeFromLeft(topW).reduced(gap, 0);
        labels[static_cast<std::size_t>(i)].setBounds(cell.removeFromTop(labelH));
        sliders[static_cast<std::size_t>(i)].setBounds(cell);
    }
    auto modeCell = top.reduced(gap, 0);
    labels.back().setBounds(modeCell.removeFromTop(labelH));
    modeBox.setBounds(modeCell.reduced(4, 8));

    const auto bottomW = bottom.getWidth() / 4;
    for (int i = 3; i < 7; ++i)
    {
        auto cell = bottom.removeFromLeft(bottomW).reduced(gap, 0);
        labels[static_cast<std::size_t>(i)].setBounds(cell.removeFromTop(labelH));
        sliders[static_cast<std::size_t>(i)].setBounds(cell);
    }
}

void SidebandMawAudioProcessorEditor::timerCallback()
{
    sidebandmaw::dsp::MawSnapshot snapshot;
    ownerProcessor.copyMawSnapshot(snapshot);
    meter.setSnapshot(snapshot);
    updateReadout();
}

void SidebandMawAudioProcessorEditor::updateReadout()
{
    sidebandmaw::dsp::MawSnapshot snapshot;
    ownerProcessor.copyMawSnapshot(snapshot);
    auto state = juce::String("LIVE");
    if (snapshot.warning)
        state = "RESCUE";

    status.setText(juce::String::formatted("%s   SHIFT %.1fHZ   IN %.4f   WET %.4f   FB %.4f",
                                           state.toRawUTF8(),
                                           snapshot.shiftHz,
                                           snapshot.inputRms,
                                           snapshot.wetRms,
                                           snapshot.feedbackEnergy),
                   juce::dontSendNotification);
}

void SidebandMawAudioProcessorEditor::configureControl(juce::Slider& slider,
                                                       juce::Label& label,
                                                       const juce::String& text)
{
    label.setComponentID("sidebandmaw-label-" + text.toLowerCase());
    design::styleLabel(label);
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);

    slider.setComponentID("sidebandmaw-control-" + text.toLowerCase());
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, text == "SHIFT" || text == "TONE" ? 62 : 48, 18);
    slider.setColour(juce::Slider::trackColourId, design::Palette::paper());
    slider.setColour(juce::Slider::backgroundColourId, design::Palette::low());
    slider.setColour(juce::Slider::thumbColourId, design::Palette::paper());
    slider.setColour(juce::Slider::textBoxTextColourId, design::Palette::paper());
    slider.setColour(juce::Slider::textBoxOutlineColourId, design::Palette::mid());
    addAndMakeVisible(slider);
}

void SidebandMawAudioProcessorEditor::MawMeter::setSnapshot(const sidebandmaw::dsp::MawSnapshot& next)
{
    snapshot = next;
    repaint();
}

void SidebandMawAudioProcessorEditor::MawMeter::paint(juce::Graphics& graphics)
{
    graphics.fillAll(design::Palette::ink());
    const auto area = getLocalBounds();
    const auto cellW = area.getWidth() / sidebandmaw::dsp::MawSnapshot::columns;
    const auto cellH = area.getHeight() / sidebandmaw::dsp::MawSnapshot::rows;

    for (int y = 0; y < sidebandmaw::dsp::MawSnapshot::rows; ++y)
    {
        for (int x = 0; x < sidebandmaw::dsp::MawSnapshot::columns; ++x)
        {
            const auto value = snapshot.cells[static_cast<std::size_t>(y * sidebandmaw::dsp::MawSnapshot::columns + x)];
            auto cell = juce::Rectangle<int>(area.getX() + x * cellW,
                                             area.getY() + y * cellH,
                                             juce::jmax(1, cellW - 1),
                                             juce::jmax(1, cellH - 1));
            graphics.setColour(value > 0.66f ? design::Palette::paper()
                              : value > 0.25f ? design::Palette::mid()
                              : design::Palette::low());
            if (value > 0.0f)
                graphics.fillRect(cell);
            else
                graphics.drawRect(cell, 1);
        }
    }

    if (snapshot.warning)
    {
        graphics.setColour(design::Palette::paper());
        for (int x = 0; x < area.getWidth(); x += 8)
            graphics.drawVerticalLine(area.getX() + x, static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));
    }

    if (hasKeyboardFocus(true))
    {
        graphics.setColour(design::Palette::paper());
        graphics.drawRect(area, 2);
    }
}
