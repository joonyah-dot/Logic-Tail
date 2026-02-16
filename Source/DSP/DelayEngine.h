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

    HighPassFilter highPassL;
    HighPassFilter highPassR;
    LowPassFilter lowPassL;
    LowPassFilter lowPassR;
};
