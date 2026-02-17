#pragma once
#include <JuceHeader.h>
#include "FilterUtils.h"

class ReverbEngine
{
public:
    ReverbEngine();

    void prepare(double sampleRate, int samplesPerBlock);
    void setGravity(float gravity);
    void setSize(float size);
    void setPreDelay(float ms);
    void setFeedback(float percent);
    void setModulation(float depthPercent, float rateHz);
    void setLoEQ(float dB);
    void setHiEQ(float dB);
    void setResonance(float percent);
    void setFreeze(bool frozen);
    void setKillDry(bool kill);
    void process(juce::AudioBuffer<float>& buffer);
    void reset();

private:
    static constexpr int kNumSharedAllpasses = 12;
    static constexpr int kNumChannelAllpasses = 8;
    static constexpr int kMaxPreDelaySamples = 96000;

    // Delay lengths at 44.1kHz (in samples)
    static constexpr int sharedDelays[kNumSharedAllpasses] = {
        1049, 1223, 1429, 1597, 1777, 1951, 2131, 2309, 2503, 2687, 2857, 3011
    };
    static constexpr int leftDelays[kNumChannelAllpasses] = {
        1051, 1249, 1453, 1627, 1801, 1979, 2153, 2333
    };
    static constexpr int rightDelays[kNumChannelAllpasses] = {
        1063, 1259, 1471, 1637, 1811, 1997, 2161, 2351
    };

    // Allpass chains
    AllPassDelay sharedAllpasses[kNumSharedAllpasses];
    AllPassDelay leftAllpasses[kNumChannelAllpasses];
    AllPassDelay rightAllpasses[kNumChannelAllpasses];

    // Pre-delay
    std::vector<float> preDelayBuffer;
    size_t preDelayMask = 0;
    int preDelayWritePos = 0;
    float preDelaySamples = 0.0f;

    // Shelving EQ filters
    juce::dsp::IIR::Filter<float> loShelfL;
    juce::dsp::IIR::Filter<float> loShelfR;
    juce::dsp::IIR::Filter<float> hiShelfL;
    juce::dsp::IIR::Filter<float> hiShelfR;

    // Dedicated feedback damping filters (always-on decay)
    juce::dsp::IIR::Filter<float> feedbackDampingL;
    juce::dsp::IIR::Filter<float> feedbackDampingR;

    // LFO phases (one per allpass)
    float sharedLfoPhases[kNumSharedAllpasses];
    float leftLfoPhases[kNumChannelAllpasses];
    float rightLfoPhases[kNumChannelAllpasses];

    // State variables
    double currentSampleRate = 44100.0;
    float sampleRateScale = 1.0f;
    float currentSize = 1.0f;
    float modDepthSamples = 0.0f;
    float lfoPhaseInc = 0.0f;
    float feedbackAmount = 0.0f;
    float prevFeedbackL = 0.0f;
    float prevFeedbackR = 0.0f;
    float currentLoEQ = 0.0f;
    float currentHiEQ = 0.0f;
    float currentResonance = 0.707f;
    bool isFrozen = false;
    bool killDrySignal = false;
};
