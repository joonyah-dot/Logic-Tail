#pragma once
#include <JuceHeader.h>

class HighPassFilter
{
public:
    HighPassFilter() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void setCutoff(float freqHz);
    float processSample(float input);
    void reset();

private:
    juce::dsp::IIR::Filter<float> filter;
    double currentSampleRate = 44100.0;
};

class LowPassFilter
{
public:
    LowPassFilter() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void setCutoff(float freqHz);
    float processSample(float input);
    void reset();

private:
    juce::dsp::IIR::Filter<float> filter;
    double currentSampleRate = 44100.0;
};

class AllPassDelay
{
public:
    explicit AllPassDelay(int maxDelaySamples);

    void prepare(double sampleRate);
    void setDelay(float delaySamples);
    void setCoefficient(float g);
    float processSample(float input);

    void setModulation(float depthSamples, float rateHz, float phaseOffset);
    float processSampleModulated(float input);

    void reset();

private:
    std::vector<float> buffer;
    size_t bufferSize = 0;
    size_t bufferMask = 0;
    int writePos = 0;

    float delaySamples = 0.0f;
    float coefficient = 0.7f;
    float delayedOutput = 0.0f;

    // Modulation
    double sampleRate = 44100.0;
    float baseDelay = 0.0f;
    float modDepth = 0.0f;
    float lfoPhase = 0.0f;
    float lfoPhaseOffset = 0.0f;
    float lfoIncrement = 0.0f;
};
