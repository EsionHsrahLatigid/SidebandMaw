#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>

namespace
{
juce::AudioProcessorParameter* findParameter(juce::AudioPluginInstance& instance,
                                              const juce::String& name)
{
    for (auto* parameter : instance.getParameters())
        if (parameter != nullptr && parameter->getName(128) == name)
            return parameter;
    return nullptr;
}

int failure(int code, const char* message)
{
    std::cerr << "[FAIL] " << message << '\n';
    return code;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "usage: SidebandMawPluginLoadTest <vst3-bundle> <expected-name>\n";
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    juce::AudioPluginFormatManager manager;
    juce::addHeadlessDefaultFormatsToManager(manager);

    juce::AudioPluginFormat* vst3 = nullptr;
    for (auto* format : manager.getFormats())
        if (format != nullptr && format->getName().containsIgnoreCase("VST3"))
            vst3 = format;
    if (vst3 == nullptr)
        return failure(3, "VST3 host format is unavailable");

    juce::OwnedArray<juce::PluginDescription> descriptions;
    vst3->findAllTypesForFile(descriptions, juce::String(argv[1]));
    if (descriptions.size() != 1)
        return failure(4, "expected exactly one VST3 class");

    auto* description = descriptions.getFirst();
    if (description == nullptr || (description->name != argv[2] && description->descriptiveName != argv[2]))
        return failure(5, "unexpected plug-in name");
    if (description->manufacturerName != "EsionHsrahLatigid" || description->isInstrument)
        return failure(6, "hosted identity should describe an EHL audio effect");

    juce::String error;
    auto instance = manager.createPluginInstance(*description, 48000.0, 256, error);
    if (instance == nullptr)
    {
        std::cerr << "VST3 instantiation failed: " << error << '\n';
        return 7;
    }

    instance->prepareToPlay(48000.0, 256);
    if (instance->getLatencySamples() != 64)
        return failure(8, "hosted latency should match the Hilbert wet path delay");

    constexpr const char* parameterNames[] {
        "Shift", "Mode", "Feedback", "Spread", "Drive", "Tone", "Mix", "Output"
    };
    for (const auto* name : parameterNames)
        if (findParameter(*instance, name) == nullptr)
            return failure(9, "hosted parameter set is incomplete");

    findParameter(*instance, "Shift")->setValueNotifyingHost(1.0f);
    findParameter(*instance, "Mode")->setValueNotifyingHost(1.0f);
    findParameter(*instance, "Feedback")->setValueNotifyingHost(0.75f);
    findParameter(*instance, "Spread")->setValueNotifyingHost(1.0f);
    findParameter(*instance, "Drive")->setValueNotifyingHost(1.0f);
    findParameter(*instance, "Tone")->setValueNotifyingHost(1.0f);
    findParameter(*instance, "Mix")->setValueNotifyingHost(1.0f);
    findParameter(*instance, "Output")->setValueNotifyingHost(1.0f);

    juce::MemoryBlock state;
    instance->getStateInformation(state);
    findParameter(*instance, "Spread")->setValueNotifyingHost(0.0f);
    instance->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    if (state.isEmpty() || findParameter(*instance, "Spread")->getValue() < 0.99f)
        return failure(10, "hosted state should round-trip");

    double sum = 0.0;
    double sumSquares = 0.0;
    float peak = 0.0f;
    int clipped = 0;
    int zeroCrossings = 0;
    float previous = 0.0f;
    bool hadPrevious = false;
    std::set<int> buckets;
    constexpr int blocks = 360;
    constexpr int blockSize = 256;

    for (int block = 0; block < blocks; ++block)
    {
        juce::AudioBuffer<float> audio(2, blockSize);
        juce::MidiBuffer midi;
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const auto position = block * blockSize + sample;
            const auto value = 0.15f * std::sin(2.0f * juce::MathConstants<float>::pi
                                                * 440.0f * static_cast<float>(position) / 48000.0f);
            audio.setSample(0, sample, value);
            audio.setSample(1, sample, value);
        }

        instance->processBlock(audio, midi);
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            {
                const auto value = audio.getSample(channel, sample);
                if (!std::isfinite(value))
                    return failure(11, "hosted render produced a non-finite sample");
                sum += value;
                sumSquares += static_cast<double>(value) * value;
                peak = std::max(peak, std::abs(value));
                clipped += std::abs(value) >= 0.979f ? 1 : 0;
                if (hadPrevious && ((previous < 0.0f && value >= 0.0f) || (previous >= 0.0f && value < 0.0f)))
                    ++zeroCrossings;
                previous = value;
                hadPrevious = true;
                buckets.insert(static_cast<int>(std::round(value * 4096.0f)));
            }
    }

    instance->releaseResources();
    const auto sampleCount = static_cast<double>(blocks * blockSize * 2);
    const auto rms = std::sqrt(sumSquares / sampleCount);
    const auto dc = sum / sampleCount;
    if (rms <= 0.02 || peak > 0.941f || std::abs(dc) >= rms * 0.8
        || clipped >= static_cast<int>(sampleCount / 20.0) || zeroCrossings <= 128 || buckets.size() <= 64)
    {
        std::cerr << "hosted audibility contract failed: rms=" << rms
                  << " peak=" << peak << " dc=" << dc
                  << " clipped=" << clipped << " crossings=" << zeroCrossings
                  << " buckets=" << buckets.size() << '\n';
        return 12;
    }

    std::cout << "loaded=" << description->name
              << " latency=" << instance->getLatencySamples()
              << " rms=" << rms << " peak=" << peak
              << " dc=" << dc << " crossings=" << zeroCrossings
              << " buckets=" << buckets.size() << '\n';
    return 0;
}
