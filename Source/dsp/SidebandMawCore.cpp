#include "SidebandMawCore.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace sidebandmaw::dsp
{
namespace
{
static_assert(std::atomic<float>::is_always_lock_free,
              "SidebandMaw publishes UI metering with lock-free atomic float stores");

constexpr float pi = 3.14159265358979323846f;
constexpr float twoPi = 2.0f * pi;

float finiteOrZero(float value) noexcept
{
    return std::isfinite(value) ? value : 0.0f;
}

float clamp01(float value) noexcept
{
    return std::clamp(finiteOrZero(value), 0.0f, 1.0f);
}

float smooth(float current, float target, float amount) noexcept
{
    return current + (target - current) * amount;
}

float dbToGain(float db) noexcept
{
    db = std::clamp(finiteOrZero(db), -24.0f, 12.0f);
    return std::pow(10.0f, db / 20.0f);
}
} // namespace

void SidebandMawCore::AtomicMawSnapshot::reset() noexcept
{
    for (auto& cell : cells)
        cell.store(0.0f, std::memory_order_relaxed);

    inputRms.store(0.0f, std::memory_order_relaxed);
    wetRms.store(0.0f, std::memory_order_relaxed);
    feedbackEnergy.store(0.0f, std::memory_order_relaxed);
    shiftHz.store(0.0f, std::memory_order_relaxed);
    mode.store(0, std::memory_order_relaxed);
    warning.store(false, std::memory_order_relaxed);
}

void SidebandMawCore::AtomicMawSnapshot::copyTo(MawSnapshot& destination) const noexcept
{
    for (std::size_t i = 0; i < destination.cells.size(); ++i)
        destination.cells[i] = cells[i].load(std::memory_order_relaxed);

    destination.inputRms = inputRms.load(std::memory_order_relaxed);
    destination.wetRms = wetRms.load(std::memory_order_relaxed);
    destination.feedbackEnergy = feedbackEnergy.load(std::memory_order_relaxed);
    destination.shiftHz = shiftHz.load(std::memory_order_relaxed);
    destination.mode = mode.load(std::memory_order_relaxed);
    destination.warning = warning.load(std::memory_order_relaxed);
}

float SidebandMawCore::DcBlocker::process(float input) noexcept
{
    input = finiteOrZero(input);
    const auto output = input - x1 + 0.995f * y1;
    x1 = input;
    y1 = output;
    return finiteOrZero(output);
}

void SidebandMawCore::DcBlocker::reset() noexcept
{
    x1 = 0.0f;
    y1 = 0.0f;
}

void SidebandMawCore::prepare(double newSampleRate, int channelIndex)
{
    sampleRate = std::isfinite(newSampleRate) && newSampleRate >= 8000.0 ? newSampleRate : 48000.0;
    channelPolarity = channelIndex == 0 ? -1.0f : 1.0f;
    setHilbertCoefficients();
    reset();
}

void SidebandMawCore::setHilbertCoefficients() noexcept
{
    constexpr auto center = latencySamples;
    for (int tap = 0; tap < hilbertTaps; ++tap)
    {
        const auto n = tap - center;
        auto coefficient = 0.0f;
        if (n != 0 && (std::abs(n) % 2) == 1)
            coefficient = 2.0f / (pi * static_cast<float>(n));

        const auto window = 0.54f - 0.46f * std::cos(twoPi * static_cast<float>(tap)
                                                     / static_cast<float>(hilbertTaps - 1));
        hilbertCoefficients[static_cast<std::size_t>(tap)] = coefficient * window;
    }
}

void SidebandMawCore::reset() noexcept
{
    hilbertRing.fill(0.0f);
    dryDelayRing.fill(0.0f);
    hilbertWrite = 0;
    dryDelayWrite = 0;
    inputDc.reset();
    outputDc.reset();
    feedbackDc.reset();
    snapshot.reset();
    phase = 0.0f;
    smoothedShift = 240.0f;
    smoothedFeedback = 0.18f;
    smoothedSpread = 0.50f;
    smoothedDrive = 0.25f;
    smoothedTone = 8000.0f;
    smoothedMix = 1.0f;
    smoothedOutput = 1.0f;
    feedbackState = 0.0f;
    toneState = 0.0f;
    inputSq = 0.0f;
    wetSq = 0.0f;
    feedbackSq = 0.0f;
    meterCounter = 0;
    meterWrite = 0;
}

float SidebandMawCore::analyticImag(float input, float& delayedReal) noexcept
{
    hilbertRing[static_cast<std::size_t>(hilbertWrite)] = input;

    float imag = 0.0f;
    for (int tap = 0; tap < hilbertTaps; ++tap)
    {
        auto index = hilbertWrite - tap;
        if (index < 0)
            index += hilbertTaps;
        imag += hilbertCoefficients[static_cast<std::size_t>(tap)]
              * hilbertRing[static_cast<std::size_t>(index)];
    }

    auto realIndex = hilbertWrite - latencySamples;
    if (realIndex < 0)
        realIndex += hilbertTaps;
    delayedReal = hilbertRing[static_cast<std::size_t>(realIndex)];

    if (++hilbertWrite >= hilbertTaps)
        hilbertWrite = 0;

    return finiteOrZero(imag);
}

float SidebandMawCore::delayDry(float input) noexcept
{
    dryDelayRing[static_cast<std::size_t>(dryDelayWrite)] = input;

    auto readIndex = dryDelayWrite - latencySamples;
    if (readIndex < 0)
        readIndex += dryDelaySize;

    const auto delayed = dryDelayRing[static_cast<std::size_t>(readIndex)];
    if (++dryDelayWrite >= dryDelaySize)
        dryDelayWrite = 0;

    return finiteOrZero(delayed);
}

float SidebandMawCore::fold(float input, float drive) noexcept
{
    const auto preGain = 1.0f + 16.0f * drive * drive;
    auto x = input * preGain;
    x = std::fmod(x + 1.0f, 4.0f);
    if (x < 0.0f)
        x += 4.0f;
    x = x - 1.0f;
    if (x > 1.0f)
        x = 2.0f - x;
    if (x < -1.0f)
        x = -2.0f - x;
    return std::tanh(x * (1.0f + 2.5f * drive));
}

float SidebandMawCore::updateTone(float input, float cutoffHz) noexcept
{
    const auto cutoff = std::clamp(finiteOrZero(cutoffHz), 20.0f, 16000.0f);
    const auto coefficient = std::clamp(twoPi * cutoff / static_cast<float>(sampleRate), 0.001f, 0.85f);
    toneState += coefficient * (input - toneState);
    return finiteOrZero(toneState);
}

float SidebandMawCore::processSample(float input, const SidebandMawParameters& params) noexcept
{
    input = inputDc.process(std::clamp(finiteOrZero(input), -4.0f, 4.0f));
    const auto dry = delayDry(input);

    smoothedShift = smooth(smoothedShift, std::clamp(finiteOrZero(params.shiftHz), 0.0f, 20000.0f), 0.0025f);
    smoothedFeedback = smooth(smoothedFeedback, std::clamp(finiteOrZero(params.feedback), 0.0f, 0.94f), 0.0025f);
    smoothedSpread = smooth(smoothedSpread, clamp01(params.spread), 0.0025f);
    smoothedDrive = smooth(smoothedDrive, clamp01(params.drive), 0.0025f);
    smoothedTone = smooth(smoothedTone, std::clamp(finiteOrZero(params.toneHz), 20.0f, 16000.0f), 0.0025f);
    smoothedMix = smooth(smoothedMix, clamp01(params.mix), 0.0025f);
    smoothedOutput = smooth(smoothedOutput, dbToGain(params.outputDb), 0.0025f);

    const auto spreadHz = (smoothedSpread - 0.5f) * channelPolarity * 180.0f;
    auto frequency = std::clamp(smoothedShift + spreadHz, 0.0f, 20000.0f);
    phase += twoPi * frequency / static_cast<float>(sampleRate);
    while (phase >= twoPi)
        phase -= twoPi;
    while (phase < 0.0f)
        phase += twoPi;

    const auto carrierCos = std::cos(phase);
    const auto carrierSin = std::sin(phase);
    const auto loopedInput = input + feedbackDc.process(feedbackState) * smoothedFeedback;
    float delayedReal = 0.0f;
    const auto imag = analyticImag(loopedInput, delayedReal);
    const auto real = delayedReal;
    const auto shifted = real * carrierCos - imag * carrierSin;
    const auto ringed = real * carrierCos * 1.65f;

    float wet = shifted;
    if (params.mode == Mode::ring)
        wet = ringed;
    else if (params.mode == Mode::maw)
    {
        const auto nonlinearInput = 0.60f * shifted + 0.40f * ringed + 0.35f * feedbackState;
        const auto midPoint = 0.5f * (nonlinearInput + feedbackState);
        const auto osA = fold(midPoint, smoothedDrive);
        const auto osB = fold(nonlinearInput, smoothedDrive);
        wet = 0.5f * (osA + osB);
    }

    const auto damped = updateTone(wet, smoothedTone);
    feedbackState = std::clamp(damped, -0.92f, 0.92f);
    feedbackSq = smooth(feedbackSq, feedbackState * feedbackState, 0.002f);

    const auto inputAbs = std::abs(input);
    const auto wetAbs = std::abs(wet);
    const auto warning = inputAbs > 0.03f && smoothedMix > 0.5f && wetAbs < 0.002f
        && (smoothedDrive > 0.65f || smoothedFeedback > 0.65f);
    snapshot.warning.store(warning, std::memory_order_relaxed);
    snapshot.mode.store(static_cast<int>(params.mode), std::memory_order_relaxed);
    if (warning)
        wet += input * (0.16f + 0.12f * smoothedDrive);

    wet = outputDc.process(wet);
    const auto mixed = dry * (1.0f - smoothedMix) + wet * smoothedMix;
    const auto limited = std::tanh(mixed * smoothedOutput * 1.08f) * 0.94f;
    const auto output = finiteOrZero(limited);
    pushMeter(input, output);
    return output;
}

void SidebandMawCore::pushMeter(float input, float wet) noexcept
{
    inputSq = smooth(inputSq, input * input, 0.004f);
    wetSq = smooth(wetSq, wet * wet, 0.004f);
    const auto inputRms = std::sqrt(std::max(0.0f, inputSq));
    const auto wetRms = std::sqrt(std::max(0.0f, wetSq));
    const auto feedbackEnergy = std::sqrt(std::max(0.0f, feedbackSq));
    snapshot.inputRms.store(inputRms, std::memory_order_relaxed);
    snapshot.wetRms.store(wetRms, std::memory_order_relaxed);
    snapshot.feedbackEnergy.store(feedbackEnergy, std::memory_order_relaxed);
    snapshot.shiftHz.store(smoothedShift, std::memory_order_relaxed);

    if (++meterCounter < 96)
        return;
    meterCounter = 0;

    for (int y = 0; y < MawSnapshot::rows - 1; ++y)
    {
        const auto next = snapshot.cells[static_cast<std::size_t>((y + 1) * MawSnapshot::columns + meterWrite)]
                              .load(std::memory_order_relaxed);
        snapshot.cells[static_cast<std::size_t>(y * MawSnapshot::columns + meterWrite)]
            .store(next, std::memory_order_relaxed);
    }

    const auto energy = std::clamp(wetRms * 6.0f, 0.0f, 1.0f);
    const auto fb = std::clamp(feedbackEnergy * 8.0f, 0.0f, 1.0f);
    const auto row = static_cast<int>(std::round(energy * static_cast<float>(MawSnapshot::rows - 1)));
    for (int y = 0; y < MawSnapshot::rows; ++y)
    {
        const auto value = y >= MawSnapshot::rows - 1 - row ? std::max(energy, fb * 0.72f) : 0.0f;
        snapshot.cells[static_cast<std::size_t>(y * MawSnapshot::columns + meterWrite)]
            .store(value, std::memory_order_relaxed);
    }

    meterWrite = (meterWrite + 1) % MawSnapshot::columns;
}

void SidebandMawCore::copySnapshot(MawSnapshot& destination) const noexcept
{
    snapshot.copyTo(destination);
}

} // namespace sidebandmaw::dsp
