#pragma once

#include <array>
#include <cstddef>

namespace sidebandmaw::dsp
{
enum class Mode
{
    shift = 0,
    ring = 1,
    maw = 2
};

struct SidebandMawParameters
{
    float shiftHz = 240.0f;
    Mode mode = Mode::shift;
    float feedback = 0.18f;
    float spread = 0.50f;
    float drive = 0.25f;
    float toneHz = 8000.0f;
    float mix = 1.0f;
    float outputDb = 0.0f;
};

struct MawSnapshot
{
    static constexpr int columns = 24;
    static constexpr int rows = 6;
    std::array<float, columns * rows> cells {};
    float inputRms = 0.0f;
    float wetRms = 0.0f;
    float feedbackEnergy = 0.0f;
    float shiftHz = 0.0f;
    int mode = 0;
    bool warning = false;
};

class SidebandMawCore
{
public:
    void prepare(double newSampleRate, int channelIndex);
    void reset() noexcept;
    float processSample(float input, const SidebandMawParameters& params) noexcept;
    void copySnapshot(MawSnapshot& destination) const noexcept;

private:
    struct Allpass1
    {
        float process(float input) noexcept;
        void reset() noexcept;
        float a = 0.0f;
        float z = 0.0f;
    };

    struct DcBlocker
    {
        float process(float input) noexcept;
        void reset() noexcept;
        float x1 = 0.0f;
        float y1 = 0.0f;
    };

    void setHilbertCoefficients() noexcept;
    float analyticImag(float input) noexcept;
    float fold(float input, float drive) noexcept;
    float updateTone(float input, float cutoffHz) noexcept;
    void pushMeter(float input, float wet) noexcept;

    std::array<Allpass1, 4> iPath;
    std::array<Allpass1, 4> qPath;
    DcBlocker inputDc;
    DcBlocker outputDc;
    DcBlocker feedbackDc;
    MawSnapshot snapshot;
    double sampleRate = 48000.0;
    float phase = 0.0f;
    float smoothedShift = 240.0f;
    float smoothedFeedback = 0.18f;
    float smoothedSpread = 0.50f;
    float smoothedDrive = 0.25f;
    float smoothedTone = 8000.0f;
    float smoothedMix = 1.0f;
    float smoothedOutput = 1.0f;
    float feedbackState = 0.0f;
    float toneState = 0.0f;
    float inputSq = 0.0f;
    float wetSq = 0.0f;
    float feedbackSq = 0.0f;
    float channelPolarity = 1.0f;
    int meterCounter = 0;
    int meterWrite = 0;
};

} // namespace sidebandmaw::dsp
