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
    // Updates only the resonance peak filter coefficients.
    // Called from setResonance, setFeedback, setLoEQ, and setHiEQ.
    // Does NOT touch shelving filters — avoids infinite recursion.
    void updateResonancePeaks();
    static constexpr int kNumSharedAllpasses = 6;      // Shorter mono chain → faster onset
    static constexpr int kNumChannelAllpasses = 10;    // Longer per-channel chains → density
    static constexpr int kMaxPreDelaySamples = 96000;

    // Delay lengths at 44.1kHz (in samples) — all prime numbers for incoherent reflections
    static constexpr int sharedDelays[kNumSharedAllpasses] = {
        1049, 1223, 1429, 1597, 1777, 1951
    };
    static constexpr int leftDelays[kNumChannelAllpasses] = {
        1051, 1249, 1453, 1627, 1801, 1979, 2153, 2333, 2521, 2699
    };
    static constexpr int rightDelays[kNumChannelAllpasses] = {
        1063, 1259, 1471, 1637, 1811, 1997, 2161, 2351, 2539, 2713
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

    // Feedback path filters (applied every iteration — cuts only, never boost)
    juce::dsp::IIR::Filter<float> feedbackDampingL;      // LP at 10kHz — hi decay
    juce::dsp::IIR::Filter<float> feedbackDampingR;
    juce::dsp::IIR::Filter<float> feedbackHPL;           // HP at 80Hz — lo decay
    juce::dsp::IIR::Filter<float> feedbackHPR;
    juce::dsp::IIR::Filter<float> resPeakLoL;            // Resonance peak at 350 Hz
    juce::dsp::IIR::Filter<float> resPeakLoR;
    juce::dsp::IIR::Filter<float> resPeakHiL;            // Resonance peak at 2000 Hz
    juce::dsp::IIR::Filter<float> resPeakHiR;
    juce::dsp::IIR::Filter<float> feedbackLoShelfL;      // Lo shelf cut-only
    juce::dsp::IIR::Filter<float> feedbackLoShelfR;
    juce::dsp::IIR::Filter<float> feedbackHiShelfL;      // Hi shelf cut-only
    juce::dsp::IIR::Filter<float> feedbackHiShelfR;

    // Output path filters (applied once before output — boost only, safe outside loop)
    juce::dsp::IIR::Filter<float> outputLoShelfL;
    juce::dsp::IIR::Filter<float> outputLoShelfR;
    juce::dsp::IIR::Filter<float> outputHiShelfL;
    juce::dsp::IIR::Filter<float> outputHiShelfR;

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
    float currentLoEQdB = 0.0f;
    float currentHiEQdB = 0.0f;
    float currentResonance = 0.0f;
    float resonanceQ = 0.707f;
    bool isFrozen = false;
    bool killDrySignal = false;
};
