#include "SidebandMawCore.h"

#include <algorithm>
#include <cmath>

namespace sidebandmaw::dsp
{
namespace
{
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

float SidebandMawCore::Allpass1::process(float input) noexcept
{
    const auto output = -a * input + z;
    z = input + a * output;
    return output;
}

void SidebandMawCore::Allpass1::reset() noexcept
{
    z = 0.0f;
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
    const std::array<float, 4> iCoefficients { 0.0417f, 0.3297f, 0.7067f, 0.9239f };
    const std::array<float, 4> qCoefficients { 0.1380f, 0.5120f, 0.8320f, 0.9820f };
    for (std::size_t i = 0; i < iPath.size(); ++i)
    {
        iPath[i].a = iCoefficients[i];
        qPath[i].a = qCoefficients[i];
    }
}

void SidebandMawCore::reset() noexcept
{
    for (auto& allpass : iPath)
        allpass.reset();
    for (auto& allpass : qPath)
        allpass.reset();
    inputDc.reset();
    outputDc.reset();
    feedbackDc.reset();
    snapshot = {};
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

float SidebandMawCore::analyticImag(float input) noexcept
{
    auto i = input;
    auto q = input;
    for (auto& allpass : iPath)
        i = allpass.process(i);
    for (auto& allpass : qPath)
        q = allpass.process(q);
    return q - 0.12f * i;
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
    if (phase >= twoPi)
        phase -= twoPi;

    const auto carrierCos = std::cos(phase);
    const auto carrierSin = std::sin(phase);
    const auto loopedInput = input + feedbackDc.process(feedbackState) * smoothedFeedback;
    const auto real = loopedInput;
    const auto imag = analyticImag(loopedInput);
    const auto shifted = real * carrierCos - imag * carrierSin;
    const auto ringed = loopedInput * carrierCos * 1.65f;

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
    snapshot.warning = inputAbs > 0.03f && smoothedMix > 0.5f && wetAbs < 0.002f
        && (smoothedDrive > 0.65f || smoothedFeedback > 0.65f);
    if (snapshot.warning)
        wet += input * (0.16f + 0.12f * smoothedDrive);

    wet = outputDc.process(wet);
    const auto mixed = input * (1.0f - smoothedMix) + wet * smoothedMix;
    const auto limited = std::tanh(mixed * smoothedOutput * 1.08f) * 0.94f;
    const auto output = finiteOrZero(limited);
    pushMeter(input, output);
    return output;
}

void SidebandMawCore::pushMeter(float input, float wet) noexcept
{
    inputSq = smooth(inputSq, input * input, 0.004f);
    wetSq = smooth(wetSq, wet * wet, 0.004f);
    snapshot.inputRms = std::sqrt(std::max(0.0f, inputSq));
    snapshot.wetRms = std::sqrt(std::max(0.0f, wetSq));
    snapshot.feedbackEnergy = std::sqrt(std::max(0.0f, feedbackSq));
    snapshot.shiftHz = smoothedShift;

    if (++meterCounter < 96)
        return;
    meterCounter = 0;

    for (int y = 0; y < MawSnapshot::rows - 1; ++y)
        snapshot.cells[static_cast<std::size_t>(y * MawSnapshot::columns + meterWrite)] =
            snapshot.cells[static_cast<std::size_t>((y + 1) * MawSnapshot::columns + meterWrite)];

    const auto energy = std::clamp(snapshot.wetRms * 6.0f, 0.0f, 1.0f);
    const auto fb = std::clamp(snapshot.feedbackEnergy * 8.0f, 0.0f, 1.0f);
    const auto row = static_cast<int>(std::round(energy * static_cast<float>(MawSnapshot::rows - 1)));
    for (int y = 0; y < MawSnapshot::rows; ++y)
    {
        const auto value = y >= MawSnapshot::rows - 1 - row ? std::max(energy, fb * 0.72f) : 0.0f;
        snapshot.cells[static_cast<std::size_t>(y * MawSnapshot::columns + meterWrite)] = value;
    }

    meterWrite = (meterWrite + 1) % MawSnapshot::columns;
}

void SidebandMawCore::copySnapshot(MawSnapshot& destination) const noexcept
{
    destination = snapshot;
}

} // namespace sidebandmaw::dsp
