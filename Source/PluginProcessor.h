#pragma once

#include "dsp/SidebandMawCore.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

class SidebandMawAudioProcessor final : public juce::AudioProcessor
{
public:
    SidebandMawAudioProcessor();
    ~SidebandMawAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.2; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    void copyMawSnapshot(sidebandmaw::dsp::MawSnapshot& destination) const noexcept;

    juce::AudioProcessorValueTreeState parameters;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    struct ParameterPointers
    {
        std::atomic<float>* shift = nullptr;
        std::atomic<float>* mode = nullptr;
        std::atomic<float>* feedback = nullptr;
        std::atomic<float>* spread = nullptr;
        std::atomic<float>* drive = nullptr;
        std::atomic<float>* tone = nullptr;
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* output = nullptr;
    } parameter;

    void cacheParameterPointers();
    [[nodiscard]] sidebandmaw::dsp::SidebandMawParameters readParameters() const noexcept;

    std::array<sidebandmaw::dsp::SidebandMawCore, 2> cores;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidebandMawAudioProcessor)
};
