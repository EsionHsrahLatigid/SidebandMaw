#include "PluginProcessor.h"
#include "ParameterIDs.h"
#include "PluginEditor.h"

#include <algorithm>

namespace
{
using APVTS = juce::AudioProcessorValueTreeState;
using Layout = APVTS::ParameterLayout;

std::unique_ptr<juce::AudioParameterFloat> makeFloat(const char* id,
                                                      const char* name,
                                                      juce::NormalisableRange<float> range,
                                                      float defaultValue)
{
    return std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id, 1 }, name, range, defaultValue);
}
} // namespace

SidebandMawAudioProcessor::SidebandMawAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, juce::Identifier("SidebandMawState"), createParameterLayout())
{
    cacheParameterPointers();
}

Layout SidebandMawAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> values;
    values.reserve(8);
    values.push_back(makeFloat(sidebandmaw::parameters::shift, "Shift", { 0.0f, 20000.0f, 0.1f }, 240.0f));
    values.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { sidebandmaw::parameters::mode, 1 }, "Mode", juce::StringArray { "Shift", "Ring", "Maw" }, 0));
    values.push_back(makeFloat(sidebandmaw::parameters::feedback, "Feedback", { 0.0f, 0.94f, 0.001f }, 0.18f));
    values.push_back(makeFloat(sidebandmaw::parameters::spread, "Spread", { 0.0f, 1.0f, 0.001f }, 0.50f));
    values.push_back(makeFloat(sidebandmaw::parameters::drive, "Drive", { 0.0f, 1.0f, 0.001f }, 0.25f));
    values.push_back(makeFloat(sidebandmaw::parameters::tone, "Tone", { 20.0f, 16000.0f, 1.0f }, 8000.0f));
    values.push_back(makeFloat(sidebandmaw::parameters::mix, "Mix", { 0.0f, 1.0f, 0.001f }, 1.0f));
    values.push_back(makeFloat(sidebandmaw::parameters::output, "Output", { -24.0f, 12.0f, 0.1f }, 0.0f));
    return { values.begin(), values.end() };
}

void SidebandMawAudioProcessor::cacheParameterPointers()
{
    parameter.shift = parameters.getRawParameterValue(sidebandmaw::parameters::shift);
    parameter.mode = parameters.getRawParameterValue(sidebandmaw::parameters::mode);
    parameter.feedback = parameters.getRawParameterValue(sidebandmaw::parameters::feedback);
    parameter.spread = parameters.getRawParameterValue(sidebandmaw::parameters::spread);
    parameter.drive = parameters.getRawParameterValue(sidebandmaw::parameters::drive);
    parameter.tone = parameters.getRawParameterValue(sidebandmaw::parameters::tone);
    parameter.mix = parameters.getRawParameterValue(sidebandmaw::parameters::mix);
    parameter.output = parameters.getRawParameterValue(sidebandmaw::parameters::output);
}

void SidebandMawAudioProcessor::prepareToPlay(double sampleRate, int)
{
    setLatencySamples(sidebandmaw::dsp::SidebandMawCore::latencySamples);
    for (std::size_t i = 0; i < cores.size(); ++i)
        cores[i].prepare(sampleRate, static_cast<int>(i));
}

void SidebandMawAudioProcessor::releaseResources()
{
}

void SidebandMawAudioProcessor::reset()
{
    for (auto& core : cores)
        core.reset();
}

bool SidebandMawAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::stereo()
        && (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
}

sidebandmaw::dsp::SidebandMawParameters SidebandMawAudioProcessor::readParameters() const noexcept
{
    sidebandmaw::dsp::SidebandMawParameters result;
    result.shiftHz = parameter.shift->load();
    const auto modeIndex = std::clamp(static_cast<int>(parameter.mode->load() + 0.5f), 0, 2);
    result.mode = static_cast<sidebandmaw::dsp::Mode>(modeIndex);
    result.feedback = parameter.feedback->load();
    result.spread = parameter.spread->load();
    result.drive = parameter.drive->load();
    result.toneHz = parameter.tone->load();
    result.mix = parameter.mix->load();
    result.outputDb = parameter.output->load();
    return result;
}

void SidebandMawAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    midiMessages.clear();

    const auto numSamples = buffer.getNumSamples();
    const auto inputChannels = std::clamp(getTotalNumInputChannels(), 1, 2);
    const auto outputChannels = std::min(buffer.getNumChannels(), 2);
    if (numSamples <= 0 || outputChannels <= 0)
        return;

    for (int channel = outputChannels; channel < buffer.getNumChannels(); ++channel)
        buffer.clear(channel, 0, numSamples);

    const auto params = readParameters();
    auto* left = buffer.getWritePointer(0);
    auto* right = outputChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto leftIn = left[sample];
        const auto rightIn = inputChannels > 1 && right != nullptr ? right[sample] : leftIn;
        left[sample] = cores[0].processSample(leftIn, params);
        if (right != nullptr)
            right[sample] = cores[1].processSample(rightIn, params);
    }
}

void SidebandMawAudioProcessor::getStateInformation(juce::MemoryBlock& destinationData)
{
    if (const auto xml = parameters.copyState().createXml())
        copyXmlToBinary(*xml, destinationData);
}

void SidebandMawAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        const auto state = juce::ValueTree::fromXml(*xml);
        if (state.isValid() && state.hasType(parameters.state.getType()))
            parameters.replaceState(state);
    }
}

void SidebandMawAudioProcessor::copyMawSnapshot(sidebandmaw::dsp::MawSnapshot& destination) const noexcept
{
    sidebandmaw::dsp::MawSnapshot left;
    sidebandmaw::dsp::MawSnapshot right;
    cores[0].copySnapshot(left);
    cores[1].copySnapshot(right);

    destination = left;
    for (std::size_t i = 0; i < destination.cells.size(); ++i)
        destination.cells[i] = std::max(left.cells[i], right.cells[i]);
    destination.inputRms = 0.5f * (left.inputRms + right.inputRms);
    destination.wetRms = 0.5f * (left.wetRms + right.wetRms);
    destination.feedbackEnergy = 0.5f * (left.feedbackEnergy + right.feedbackEnergy);
    destination.shiftHz = 0.5f * (left.shiftHz + right.shiftHz);
    destination.warning = left.warning || right.warning;
}

juce::AudioProcessorEditor* SidebandMawAudioProcessor::createEditor()
{
    return new SidebandMawAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SidebandMawAudioProcessor();
}
