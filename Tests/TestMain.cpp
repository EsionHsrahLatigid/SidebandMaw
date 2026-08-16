#include "../Source/dsp/SidebandMawCore.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

namespace
{
using namespace sidebandmaw::dsp;

struct Failure final : std::exception
{
    explicit Failure(std::string messageIn) : message(std::move(messageIn)) {}
    const char* what() const noexcept override { return message.c_str(); }
    std::string message;
};

struct Metrics
{
    float rms = 0.0f;
    float peak = 0.0f;
    float dc = 0.0f;
    int zeroCrossings = 0;
    int clipped = 0;
    int uniqueBuckets = 0;
};

[[noreturn]] void fail(const std::string& message)
{
    throw Failure(message);
}

void expect(bool condition, const std::string& message)
{
    if (!condition)
        fail(message);
}

void near(float actual, float expected, float tolerance, const std::string& message)
{
    if (std::abs(actual - expected) > tolerance)
        fail(message + " actual=" + std::to_string(actual) + " expected=" + std::to_string(expected));
}

Metrics measure(const std::vector<float>& signal)
{
    Metrics result;
    double sum = 0.0;
    double sumSquares = 0.0;
    bool hadPrevious = false;
    float previous = 0.0f;
    std::vector<int> buckets;
    buckets.reserve(signal.size());

    for (auto sample : signal)
    {
        expect(std::isfinite(sample), "output must be finite");
        sum += sample;
        sumSquares += static_cast<double>(sample) * sample;
        result.peak = std::max(result.peak, std::abs(sample));
        if (std::abs(sample) >= 0.979f)
            ++result.clipped;
        if (hadPrevious && ((previous < 0.0f && sample >= 0.0f) || (previous >= 0.0f && sample < 0.0f)))
            ++result.zeroCrossings;
        previous = sample;
        hadPrevious = true;
        buckets.push_back(static_cast<int>(std::round(sample * 4096.0f)));
    }

    std::sort(buckets.begin(), buckets.end());
    result.uniqueBuckets = static_cast<int>(std::unique(buckets.begin(), buckets.end()) - buckets.begin());
    result.rms = signal.empty() ? 0.0f : static_cast<float>(std::sqrt(sumSquares / static_cast<double>(signal.size())));
    result.dc = signal.empty() ? 0.0f : static_cast<float>(sum / static_cast<double>(signal.size()));
    return result;
}

std::vector<float> sine(float hz, std::size_t samples, float amplitude = 0.35f, float sampleRate = 48000.0f)
{
    std::vector<float> result(samples);
    for (std::size_t i = 0; i < samples; ++i)
        result[i] = amplitude * std::sin(2.0f * 3.14159265358979323846f * hz * static_cast<float>(i) / sampleRate);
    return result;
}

std::vector<float> seededNoise(std::size_t samples)
{
    std::vector<float> result(samples);
    unsigned state = 0x8da6b343u;
    for (auto& sample : result)
    {
        state = state * 1664525u + 1013904223u;
        sample = (static_cast<float>((state >> 8u) & 0xffffu) / 32768.0f - 1.0f) * 0.25f;
    }
    return result;
}

std::vector<float> render(SidebandMawParameters params,
                          const std::vector<float>& input,
                          const std::vector<int>& partitions,
                          double sampleRate = 48000.0)
{
    auto core = std::make_unique<SidebandMawCore>();
    core->prepare(sampleRate, 0);
    std::vector<float> output;
    output.reserve(input.size());
    std::size_t index = 0;
    std::size_t partition = 0;
    while (index < input.size())
    {
        const auto count = std::min<std::size_t>(static_cast<std::size_t>(partitions[partition % partitions.size()]),
                                                 input.size() - index);
        for (std::size_t i = 0; i < count; ++i)
            output.push_back(core->processSample(input[index++], params));
        ++partition;
    }
    return output;
}

float dftMagnitude(const std::vector<float>& signal, float hz, std::size_t start, float sampleRate = 48000.0f)
{
    double real = 0.0;
    double imag = 0.0;
    constexpr double twoPi = 6.28318530717958647692;
    for (std::size_t i = start; i < signal.size(); ++i)
    {
        const auto phase = twoPi * static_cast<double>(hz) * static_cast<double>(i - start) / sampleRate;
        real += signal[i] * std::cos(phase);
        imag -= signal[i] * std::sin(phase);
    }
    return static_cast<float>(std::sqrt(real * real + imag * imag) / static_cast<double>(signal.size() - start));
}

void silence_and_nonfinite_are_guarded()
{
    SidebandMawParameters params;
    params.mode = Mode::maw;
    params.feedback = 0.94f;
    params.drive = 1.0f;
    auto silence = render(params, std::vector<float>(12000, 0.0f), { 17, 64, 511 });
    expect(measure(silence).rms == 0.0f, "silence should remain exact silence");

    auto core = std::make_unique<SidebandMawCore>();
    core->prepare(96000.0, 1);
    for (int i = 0; i < 4000; ++i)
    {
        const auto input = i == 32 ? INFINITY : (i == 153 ? NAN : 0.1f);
        expect(std::isfinite(core->processSample(input, params)), "non-finite input should not propagate");
    }
}

void upper_sideband_shift_rejects_lower_sideband()
{
    struct Case
    {
        float sampleRate;
        float inputHz;
        float shiftHz;
    };

    const Case cases[] {
        { 44100.0f, 440.0f, 600.0f },
        { 48000.0f, 700.0f, 1200.0f },
        { 96000.0f, 1100.0f, 3000.0f }
    };

    for (const auto test : cases)
    {
        SidebandMawParameters params;
        params.shiftHz = test.shiftHz;
        params.mode = Mode::shift;
        params.feedback = 0.0f;
        params.spread = 0.5f;
        params.drive = 0.0f;
        params.mix = 1.0f;

        const auto samples = static_cast<std::size_t>(test.sampleRate * 2.0f);
        auto output = render(params, sine(test.inputHz, samples, 0.35f, test.sampleRate), { 128 },
                             static_cast<double>(test.sampleRate));
        const auto start = static_cast<std::size_t>(test.sampleRate * 0.75f);
        const auto carrier = dftMagnitude(output, test.inputHz, start, test.sampleRate);
        const auto upper = dftMagnitude(output, test.inputHz + test.shiftHz, start, test.sampleRate);
        const auto lower = dftMagnitude(output, std::abs(test.inputHz - test.shiftHz), start, test.sampleRate);
        const auto rejectionDb = 20.0f * std::log10((upper + 0.000001f) / (lower + 0.000001f));
        std::cout << "shift sr=" << test.sampleRate << " input=" << test.inputHz
                  << " shift=" << test.shiftHz << " carrier=" << carrier
                  << " upper=" << upper << " lower=" << lower
                  << " rejectDb=" << rejectionDb << '\n';
        expect(upper > carrier * 20.0f, "upper-sideband shift should suppress the original tone after warmup");
        expect(rejectionDb >= 15.0f, "upper-sideband shift should reject the lower sideband by at least 15 dB");
        expect(measure(output).rms > 0.03f, "shifted output should remain audible");
    }
}

void settled_impulse_respects_reported_latency_for_all_mix_values()
{
    for (const auto mix : { 0.0f, 0.5f, 1.0f })
    {
        SidebandMawCore core;
        core.prepare(48000.0, 0);
        SidebandMawParameters params;
        params.shiftHz = 0.0f;
        params.mode = Mode::ring;
        params.feedback = 0.0f;
        params.spread = 0.5f;
        params.drive = 0.0f;
        params.toneHz = 16000.0f;
        params.mix = mix;
        params.outputDb = -6.0f;

        for (int i = 0; i < 24000; ++i)
            (void) core.processSample(0.0f, params);

        std::vector<float> output(256, 0.0f);
        output[0] = core.processSample(0.5f, params);
        for (std::size_t i = 1; i < output.size(); ++i)
            output[i] = core.processSample(0.0f, params);

        near(output.front(), 0.0f, 0.000001f, "settled impulse should have no direct sample-zero output");
        for (int i = 1; i < SidebandMawCore::latencySamples; ++i)
            near(output[static_cast<std::size_t>(i)], 0.0f, 0.0001f,
                 "settled impulse should have no meaningful pre-latency output");

        const auto peakIt = std::max_element(output.begin(), output.end(),
                                             [](float a, float b) { return std::abs(a) < std::abs(b); });
        const auto peakIndex = static_cast<int>(peakIt - output.begin());
        float totalEnergy = 0.0f;
        float latencyWindowEnergy = 0.0f;
        for (std::size_t i = 0; i < output.size(); ++i)
        {
            const auto energy = output[i] * output[i];
            totalEnergy += energy;
            if (std::abs(static_cast<int>(i) - SidebandMawCore::latencySamples) <= 2)
                latencyWindowEnergy += energy;
        }

        std::cout << "impulse mix=" << mix << " peakIndex=" << peakIndex
                  << " peak=" << *peakIt << " latencyWindowEnergy=" << latencyWindowEnergy
                  << " totalEnergy=" << totalEnergy << '\n';
        expect(peakIndex == SidebandMawCore::latencySamples,
               "settled impulse peak should align with reported latency");
        expect(std::abs(*peakIt) > 0.20f, "settled impulse should keep audible latency-aligned energy");
        expect(latencyWindowEnergy > totalEnergy * 0.65f,
               "settled impulse energy should be concentrated at reported latency");
    }
}

void ring_mode_exposes_sum_and_difference_sidebands()
{
    SidebandMawParameters params;
    params.shiftHz = 600.0f;
    params.mode = Mode::ring;
    params.feedback = 0.0f;
    params.drive = 0.0f;
    params.mix = 1.0f;

    auto output = render(params, sine(440.0f, 48000), { 256 });
    const auto carrier = dftMagnitude(output, 440.0f, 16000);
    const auto upper = dftMagnitude(output, 1040.0f, 16000);
    const auto lower = dftMagnitude(output, 160.0f, 16000);
    std::cout << "ring dft carrier=" << carrier << " upper=" << upper << " lower=" << lower << '\n';
    expect(upper > carrier * 2.0f, "ring mode should create a strong upper sideband");
    expect(lower > carrier * 2.0f, "ring mode should create a strong lower sideband");
}

void maw_extremes_stay_aggressive_and_bounded()
{
    SidebandMawParameters params;
    params.shiftHz = 19000.0f;
    params.mode = Mode::maw;
    params.feedback = 0.94f;
    params.spread = 1.0f;
    params.drive = 1.0f;
    params.toneHz = 16000.0f;
    params.mix = 1.0f;
    params.outputDb = 12.0f;
    auto output = render(params, seededNoise(96000), { 17, 31, 64, 509, 1024 });
    const auto metrics = measure(output);
    std::cout << "maw rms=" << metrics.rms << " peak=" << metrics.peak
              << " dc=" << metrics.dc << " zc=" << metrics.zeroCrossings
              << " unique=" << metrics.uniqueBuckets << " clipped=" << metrics.clipped << '\n';
    expect(metrics.rms > 0.08f, "extreme maw output should not collapse into near silence");
    expect(metrics.peak <= 0.981f, "final ceiling should bound output");
    expect(metrics.clipped < 64, "output should not become a clipped constant");
    expect(std::abs(metrics.dc) < 0.08f, "DC guard should control offset");
    expect(metrics.zeroCrossings > 1000, "extreme maw output should retain high activity");
    expect(metrics.uniqueBuckets > 256, "extreme maw output should not become stationary");
}

void block_partition_determinism_and_reset()
{
    SidebandMawParameters params;
    params.shiftHz = 2300.0f;
    params.mode = Mode::maw;
    params.feedback = 0.72f;
    params.spread = 0.83f;
    params.drive = 0.88f;
    params.toneHz = 7200.0f;
    auto input = seededNoise(32000);

    auto first = render(params, input, { 64 });
    auto second = render(params, input, { 64 });
    expect(first == second, "same render path should be deterministic after reset");

    auto partitioned = render(params, input, { 1, 7, 31, 129, 511 });
    expect(first.size() == partitioned.size(), "partitioned render size should match");
    for (std::size_t i = 0; i < first.size(); ++i)
        near(first[i], partitioned[i], 0.0f, "sample-by-sample processing should be independent of host block partitioning");
}

void snapshot_reports_functional_state()
{
    SidebandMawCore core;
    core.prepare(48000.0, 0);
    SidebandMawParameters params;
    params.shiftHz = 1200.0f;
    params.mode = Mode::maw;
    params.drive = 0.7f;
    for (int i = 0; i < 12000; ++i)
        (void) core.processSample(0.2f * std::sin(0.03f * static_cast<float>(i)), params);

    MawSnapshot snapshot;
    core.copySnapshot(snapshot);
    expect(snapshot.inputRms > 0.01f, "snapshot should report input activity");
    expect(snapshot.wetRms > 0.01f, "snapshot should report wet activity");
    expect(snapshot.shiftHz > 250.0f, "snapshot should report smoothed shift");
    expect(snapshot.mode == static_cast<int>(Mode::maw), "snapshot should publish the active mode");
    const auto active = std::count_if(snapshot.cells.begin(), snapshot.cells.end(), [](float v) { return v > 0.0f; });
    expect(active > 0, "snapshot matrix should contain activity cells");
}

void low_sample_rate_extreme_shift_keeps_phase_bounded()
{
    SidebandMawCore core;
    core.prepare(8000.0, 0);
    SidebandMawParameters params;
    params.shiftHz = 20000.0f;
    params.mode = Mode::shift;
    params.feedback = 0.94f;
    params.spread = 1.0f;
    params.drive = 1.0f;
    params.toneHz = 16000.0f;
    params.mix = 1.0f;
    params.outputDb = 12.0f;

    std::vector<float> output;
    output.reserve(64000);
    for (int i = 0; i < 64000; ++i)
    {
        const auto input = 0.25f * std::sin(2.0f * 3.14159265358979323846f * 330.0f
                                            * static_cast<float>(i) / 8000.0f);
        output.push_back(core.processSample(input, params));
    }

    const auto metrics = measure(output);
    expect(metrics.rms > 0.02f, "low-rate maximum shift should remain audible");
    expect(metrics.peak <= 0.981f, "low-rate maximum shift should remain bounded");
    expect(metrics.uniqueBuckets > 128, "low-rate maximum shift should not collapse into a constant");
}

void snapshot_copy_is_safe_under_concurrent_stress()
{
    SidebandMawCore core;
    core.prepare(48000.0, 0);
    SidebandMawParameters params;
    params.shiftHz = 1800.0f;
    params.mode = Mode::maw;
    params.feedback = 0.75f;
    params.drive = 0.9f;

    std::atomic<bool> running { true };
    std::atomic<int> badReads { 0 };
    std::thread reader([&] {
        while (running.load(std::memory_order_relaxed))
        {
            MawSnapshot snapshot;
            core.copySnapshot(snapshot);
            if (!std::isfinite(snapshot.inputRms) || !std::isfinite(snapshot.wetRms)
                || !std::isfinite(snapshot.feedbackEnergy) || !std::isfinite(snapshot.shiftHz))
                badReads.fetch_add(1, std::memory_order_relaxed);
            for (auto value : snapshot.cells)
                if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
                    badReads.fetch_add(1, std::memory_order_relaxed);
        }
    });

    for (int i = 0; i < 120000; ++i)
    {
        const auto input = 0.2f * std::sin(0.011f * static_cast<float>(i))
                         + 0.04f * std::sin(0.071f * static_cast<float>(i));
        const auto output = core.processSample(input, params);
        expect(std::isfinite(output), "race stress output should remain finite");
    }

    running.store(false, std::memory_order_relaxed);
    reader.join();
    expect(badReads.load(std::memory_order_relaxed) == 0, "snapshot reader should not observe invalid values");
}

} // namespace

int main()
{
    try
    {
        silence_and_nonfinite_are_guarded();
        upper_sideband_shift_rejects_lower_sideband();
        settled_impulse_respects_reported_latency_for_all_mix_values();
        ring_mode_exposes_sum_and_difference_sidebands();
        maw_extremes_stay_aggressive_and_bounded();
        block_partition_determinism_and_reset();
        snapshot_reports_functional_state();
        low_sample_rate_extreme_shift_keeps_phase_bounded();
        snapshot_copy_is_safe_under_concurrent_stress();
        std::cout << "SidebandMaw DSP tests passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[FAIL] " << exception.what() << '\n';
        return 1;
    }
}
