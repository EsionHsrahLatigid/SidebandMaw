#include "ParameterIDs.h"
#include "PluginProcessor.h"

#include <juce_events/juce_events.h>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>

namespace
{
bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "[FAIL] " << message << '\n';
    return condition;
}

bool checkNear(float actual, float expected, float tolerance, const char* message)
{
    return check(std::abs(actual - expected) <= tolerance, message);
}

bool checkFloatParameter(SidebandMawAudioProcessor& processor,
                         const char* id,
                         float start,
                         float end,
                         float interval,
                         float defaultValue)
{
    auto* parameter = processor.parameters.getParameter(id);
    if (!check(parameter != nullptr, (std::string("missing parameter ") + id).c_str()))
        return false;

    auto* floatParameter = dynamic_cast<juce::AudioParameterFloat*>(parameter);
    bool passed = check(floatParameter != nullptr, (std::string("parameter should be float ") + id).c_str());
    if (floatParameter != nullptr)
    {
        passed &= checkNear(floatParameter->range.start, start, 0.0001f, "float range start should match");
        passed &= checkNear(floatParameter->range.end, end, 0.0001f, "float range end should match");
        passed &= checkNear(floatParameter->range.interval, interval, 0.0001f, "float range interval should match");
        passed &= checkNear(processor.parameters.getRawParameterValue(id)->load(), defaultValue, 0.0001f,
                            "float default should match");
    }
    return passed;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    auto processor = std::make_unique<SidebandMawAudioProcessor>();
    bool passed = true;

    passed &= check(processor->getName() == "SidebandMaw", "product name should be SidebandMaw");
    passed &= check(!processor->acceptsMidi(), "processor should not accept MIDI");
    passed &= check(!processor->isMidiEffect(), "processor should be an audio effect");
    passed &= check(processor->getLatencySamples() == 0, "processor should report zero latency");

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
    passed &= check(editor != nullptr, "processor should create an editor");
    if (editor != nullptr)
    {
        passed &= check(editor->getWidth() == 512 && editor->getHeight() == 320,
                        "editor should use the compact 512x320 workflow study");
        passed &= check(editor->findChildWithID("sidebandmaw-sideband-meter") != nullptr,
                        "editor should expose the sideband meter");
        passed &= check(editor->findChildWithID("sidebandmaw-control-shift") != nullptr,
                        "editor should expose the shift control");
        passed &= check(editor->findChildWithID("sidebandmaw-mode") != nullptr,
                        "editor should expose the mode selector");
        editor->setBounds(0, 0, 512, 320);
        editor->resized();
    }

    juce::AudioProcessor::BusesLayout monoToStereo;
    monoToStereo.inputBuses.add(juce::AudioChannelSet::mono());
    monoToStereo.outputBuses.add(juce::AudioChannelSet::stereo());
    passed &= check(processor->isBusesLayoutSupported(monoToStereo), "mono input/stereo output should be supported");

    passed &= check(processor->getParameters().size() == 8,
                    "processor should expose exactly eight public controls");
    passed &= checkFloatParameter(*processor, sidebandmaw::parameters::shift, 0.0f, 20000.0f, 0.1f, 240.0f);
    passed &= checkFloatParameter(*processor, sidebandmaw::parameters::feedback, 0.0f, 0.94f, 0.001f, 0.18f);
    passed &= checkFloatParameter(*processor, sidebandmaw::parameters::spread, 0.0f, 1.0f, 0.001f, 0.50f);
    passed &= checkFloatParameter(*processor, sidebandmaw::parameters::drive, 0.0f, 1.0f, 0.001f, 0.25f);
    passed &= checkFloatParameter(*processor, sidebandmaw::parameters::tone, 20.0f, 16000.0f, 1.0f, 8000.0f);
    passed &= checkFloatParameter(*processor, sidebandmaw::parameters::mix, 0.0f, 1.0f, 0.001f, 1.0f);
    passed &= checkFloatParameter(*processor, sidebandmaw::parameters::output, -24.0f, 12.0f, 0.1f, 0.0f);

    auto* mode = processor->parameters.getParameter(sidebandmaw::parameters::mode);
    passed &= check(dynamic_cast<juce::AudioParameterChoice*>(mode) != nullptr,
                    "Mode parameter should be a choice");

    auto* shift = processor->parameters.getParameter(sidebandmaw::parameters::shift);
    if (shift != nullptr && mode != nullptr)
    {
        shift->setValueNotifyingHost(shift->convertTo0to1(1300.0f));
        mode->setValueNotifyingHost(1.0f);
        juce::MemoryBlock state;
        processor->getStateInformation(state);
        shift->setValueNotifyingHost(shift->convertTo0to1(12.0f));
        mode->setValueNotifyingHost(0.0f);
        processor->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        passed &= check(std::abs(processor->parameters.getRawParameterValue(sidebandmaw::parameters::shift)->load() - 1300.0f) < 0.1f,
                        "APVTS float state should round-trip without serializing transient history");
        passed &= check(processor->parameters.getRawParameterValue(sidebandmaw::parameters::mode)->load() > 1.5f,
                        "APVTS choice state should round-trip without serializing transient history");
    }

    constexpr double sampleRate = 48000.0;
    processor->prepareToPlay(sampleRate, 1024);
    passed &= check(processor->getLatencySamples() == 0, "prepared processor should remain zero latency");
    const int blockSizes[] { 32, 64, 127, 256, 511, 1024 };
    int generatedSamples = 0;
    for (const auto blockSize : blockSizes)
    {
        juce::AudioBuffer<float> audio(2, blockSize);
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const auto value = static_cast<float>(0.2 * std::sin(2.0 * juce::MathConstants<double>::pi
                                                                 * 330.0 * generatedSamples / sampleRate));
            audio.setSample(0, sample, value);
            audio.setSample(1, sample, value);
            ++generatedSamples;
        }
        juce::MidiBuffer midi;
        processor->processBlock(audio, midi);
        passed &= check(midi.isEmpty(), "processor should clear MIDI");
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            for (int sample = 0; sample < audio.getNumSamples(); ++sample)
                passed &= check(std::isfinite(audio.getSample(channel, sample)), "processed audio should remain finite");
    }

    processor->reset();
    juce::AudioBuffer<float> silence(2, 2048);
    silence.clear();
    juce::MidiBuffer midi;
    processor->processBlock(silence, midi);
    float silencePeak = 0.0f;
    for (int channel = 0; channel < silence.getNumChannels(); ++channel)
        silencePeak = std::max(silencePeak, silence.getMagnitude(channel, 0, silence.getNumSamples()));
    passed &= check(silencePeak == 0.0f, "silence in should remain silence out");

    if (passed)
        std::cout << "SidebandMaw plug-in integration checks passed\n";
    return passed ? 0 : 1;
}
