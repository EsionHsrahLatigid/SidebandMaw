#pragma once

#include "PluginProcessor.h"
#include <ehl/juce_design/EhlDesign.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <array>
#include <memory>

class SidebandMawAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer
{
public:
    explicit SidebandMawAudioProcessorEditor(SidebandMawAudioProcessor&);
    ~SidebandMawAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int defaultWidth = 512;
    static constexpr int defaultHeight = 320;

private:
    class MawMeter final : public juce::Component
    {
    public:
        void setSnapshot(const sidebandmaw::dsp::MawSnapshot& next);
        void paint(juce::Graphics&) override;
    private:
        sidebandmaw::dsp::MawSnapshot snapshot;
    };

    void timerCallback() override;
    void updateReadout();
    void configureControl(juce::Slider& slider, juce::Label& label, const juce::String& text);

    SidebandMawAudioProcessor& ownerProcessor;
    ehl::juce_design::LookAndFeel lookAndFeel;
    MawMeter meter;
    juce::Label status;
    std::array<juce::Slider, 7> sliders;
    std::array<juce::Label, 8> labels;
    juce::ComboBox modeBox;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 7> sliderAttachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidebandMawAudioProcessorEditor)
};
