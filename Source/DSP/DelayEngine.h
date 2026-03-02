#pragma once
#include <JuceHeader.h>
#include "FilterUtils.h"

class DelayEngine
{
public:
    DelayEngine() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void setDelayTime(float timeMs);
    void setFeedback(float feedbackPercent);
    void setHighPassFreq(float hz);
    void setLowPassFreq(float hz);
    void setTempoSync(bool enabled, double bpm, int divisionIndex);
    void setPingPong(bool enabled);
    void setModulation(float rateHz, float depthPercent);
    void process(juce::AudioBuffer<float>& buffer);
    void reset();

private:
    float cubicHermite(float y0, float y1, float y2, float y3, float frac);

    std::vector<float> delayBufferL;
    std::vector<float> delayBufferR;
    size_t bufferMask = 0;
    int writePos = 0;

    double currentSampleRate = 44100.0;
    float delaySamples = 0.0f;
    float feedbackAmount = 0.0f;

    bool pingPongEnabled    = false;
    bool tempoSyncEnabled   = false;

    // Modulation LFO (normalized phase [0,1))
    float modLfoPhaseL    = 0.0f;
    float modLfoPhaseR    = 0.25f;  // 90° offset for stereo width
    float modLfoInc       = 0.0f;   // cycles per sample
    float modDepthSamples = 0.0f;

    // One-pole delay-time smoother (prevents clicks on tempo-sync jumps)
    // τ ≈ 42ms at 44100 Hz (2000 samples)
    float targetDelaySamples   = 0.0f;
    float smoothedDelaySamples = 0.0f;
    static constexpr float kSmoothCoeff = 0.9995f;

    HighPassFilter highPassL;
    HighPassFilter highPassR;
    LowPassFilter lowPassL;
    LowPassFilter lowPassR;
};
