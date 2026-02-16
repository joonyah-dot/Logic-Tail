#include "ReverbEngine.h"

ReverbEngine::ReverbEngine()
{
    // Initialize LFO phases with even distribution to decorrelate modulation
    for (int i = 0; i < kNumSharedAllpasses; ++i)
    {
        sharedLfoPhases[i] = (juce::MathConstants<float>::twoPi * i) / kNumSharedAllpasses;
    }

    for (int i = 0; i < kNumChannelAllpasses; ++i)
    {
        // Offset left and right phases for decorrelation
        leftLfoPhases[i] = (juce::MathConstants<float>::twoPi * i) / kNumChannelAllpasses + 0.5f;
        rightLfoPhases[i] = (juce::MathConstants<float>::twoPi * i) / kNumChannelAllpasses + 1.3f;
    }
}

void ReverbEngine::prepare(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    sampleRateScale = static_cast<float>(sampleRate / 44100.0);

    // Initialize shared allpass chain
    for (int i = 0; i < kNumSharedAllpasses; ++i)
    {
        int baseDelay = sharedDelays[i];
        int maxDelay = static_cast<int>(baseDelay * sampleRateScale * 1.3f);
        sharedAllpasses[i].init(maxDelay);
        sharedAllpasses[i].prepare(sampleRate);
        sharedAllpasses[i].setDelay(baseDelay * sampleRateScale * currentSize);
    }

    // Initialize left channel allpasses
    for (int i = 0; i < kNumChannelAllpasses; ++i)
    {
        int baseDelay = leftDelays[i];
        int maxDelay = static_cast<int>(baseDelay * sampleRateScale * 1.3f);
        leftAllpasses[i].init(maxDelay);
        leftAllpasses[i].prepare(sampleRate);
        leftAllpasses[i].setDelay(baseDelay * sampleRateScale * currentSize);
    }

    // Initialize right channel allpasses
    for (int i = 0; i < kNumChannelAllpasses; ++i)
    {
        int baseDelay = rightDelays[i];
        int maxDelay = static_cast<int>(baseDelay * sampleRateScale * 1.3f);
        rightAllpasses[i].init(maxDelay);
        rightAllpasses[i].prepare(sampleRate);
        rightAllpasses[i].setDelay(baseDelay * sampleRateScale * currentSize);
    }

    // Allocate pre-delay buffer (power of 2 for efficient wrapping)
    int preDelaySize = juce::nextPowerOfTwo(kMaxPreDelaySamples);
    preDelayBuffer.resize(preDelaySize, 0.0f);
    preDelayMask = preDelaySize - 1;
    preDelayWritePos = 0;

    // Prepare shelving EQ filters
    juce::dsp::ProcessSpec spec{sampleRate, static_cast<juce::uint32>(samplesPerBlock), 1};
    loShelfL.prepare(spec);
    loShelfR.prepare(spec);
    hiShelfL.prepare(spec);
    hiShelfR.prepare(spec);

    // Initialize filter coefficients
    setLoEQ(0.0f);
    setHiEQ(0.0f);

    reset();
}

void ReverbEngine::setGravity(float gravity)
{
    // Map gravity to allpass coefficient
    float g;
    if (gravity >= 0.0f)
    {
        // Positive: 0 → 0.5, 100 → 0.3
        g = juce::jmap(gravity, 0.0f, 100.0f, 0.5f, 0.3f);
    }
    else
    {
        // Negative: -100 → -0.7, 0 → 0.5
        g = juce::jmap(gravity, -100.0f, 0.0f, -0.7f, 0.5f);
    }

    // Apply coefficient to all allpasses
    for (int i = 0; i < kNumSharedAllpasses; ++i)
        sharedAllpasses[i].setCoefficient(g);

    for (int i = 0; i < kNumChannelAllpasses; ++i)
    {
        leftAllpasses[i].setCoefficient(g);
        rightAllpasses[i].setCoefficient(g);
    }
}

void ReverbEngine::setSize(float size)
{
    // Map 0-120 to scale factor
    float scaleFactor = size / 100.0f;
    scaleFactor = juce::jlimit(0.05f, 1.3f, scaleFactor);
    currentSize = scaleFactor;

    // Update all allpass delay lengths
    for (int i = 0; i < kNumSharedAllpasses; ++i)
    {
        float newDelay = sharedDelays[i] * sampleRateScale * currentSize;
        sharedAllpasses[i].setDelay(newDelay);
    }

    for (int i = 0; i < kNumChannelAllpasses; ++i)
    {
        leftAllpasses[i].setDelay(leftDelays[i] * sampleRateScale * currentSize);
        rightAllpasses[i].setDelay(rightDelays[i] * sampleRateScale * currentSize);
    }
}

void ReverbEngine::setPreDelay(float ms)
{
    preDelaySamples = (ms / 1000.0f) * static_cast<float>(currentSampleRate);
    preDelaySamples = juce::jlimit(0.0f, static_cast<float>(kMaxPreDelaySamples - 1), preDelaySamples);
}

void ReverbEngine::setFeedback(float percent)
{
    feedbackAmount = percent / 100.0f;
    feedbackAmount = juce::jlimit(0.0f, 0.99f, feedbackAmount);
}

void ReverbEngine::setModulation(float depthPercent, float rateHz)
{
    // Map depth 0-100% to 0-12 samples
    modDepthSamples = juce::jmap(depthPercent, 0.0f, 100.0f, 0.0f, 12.0f);

    // Calculate LFO phase increment
    lfoPhaseInc = (rateHz * juce::MathConstants<float>::twoPi) / static_cast<float>(currentSampleRate);
}

void ReverbEngine::setLoEQ(float dB)
{
    currentLoEQ = dB;

    // Low shelf at 350 Hz
    loShelfL.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
        currentSampleRate, 350.0f, currentResonance, juce::Decibels::decibelsToGain(dB));
    loShelfR.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
        currentSampleRate, 350.0f, currentResonance, juce::Decibels::decibelsToGain(dB));
}

void ReverbEngine::setHiEQ(float dB)
{
    currentHiEQ = dB;

    // High shelf at 2000 Hz
    hiShelfL.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        currentSampleRate, 2000.0f, currentResonance, juce::Decibels::decibelsToGain(dB));
    hiShelfR.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        currentSampleRate, 2000.0f, currentResonance, juce::Decibels::decibelsToGain(dB));
}

void ReverbEngine::setResonance(float percent)
{
    // Map 0-100% to Q factor 0.707-4.0
    currentResonance = juce::jmap(percent, 0.0f, 100.0f, 0.707f, 4.0f);

    // Update shelving filters with new Q
    setLoEQ(currentLoEQ);
    setHiEQ(currentHiEQ);
}

void ReverbEngine::setFreeze(bool frozen)
{
    isFrozen = frozen;
}

void ReverbEngine::setKillDry(bool kill)
{
    killDrySignal = kill;
}

void ReverbEngine::process(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels == 0)
        return;

    auto* leftData = buffer.getWritePointer(0);
    auto* rightData = numChannels > 1 ? buffer.getWritePointer(1) : leftData;

    for (int n = 0; n < numSamples; ++n)
    {
        // 1. Sum stereo input to mono
        float monoIn = (leftData[n] + rightData[n]) * 0.5f;

        // 2. If freeze is active, kill input
        if (isFrozen)
            monoIn = 0.0f;

        // 3. Add feedback from previous iteration (average of L/R)
        monoIn += (prevFeedbackL + prevFeedbackR) * 0.5f * feedbackAmount;

        // 4. Soft-clip to prevent blowup at high feedback
        monoIn = std::tanh(monoIn);

        // 5. Pre-delay
        preDelayBuffer[preDelayWritePos] = monoIn;
        int readIdx = static_cast<int>(preDelayWritePos - preDelaySamples) & preDelayMask;
        monoIn = preDelayBuffer[readIdx];
        preDelayWritePos = (preDelayWritePos + 1) & preDelayMask;

        // 6. Process through shared allpass chain (mono, in series)
        float signal = monoIn;
        for (int i = 0; i < kNumSharedAllpasses; ++i)
        {
            // Calculate LFO modulation for this allpass
            float lfoValue = std::sin(sharedLfoPhases[i]) * modDepthSamples;
            sharedLfoPhases[i] += lfoPhaseInc;
            if (sharedLfoPhases[i] >= juce::MathConstants<float>::twoPi)
                sharedLfoPhases[i] -= juce::MathConstants<float>::twoPi;

            // Set modulation offset and process
            sharedAllpasses[i].setModOffset(isFrozen ? 0.0f : lfoValue);
            signal = sharedAllpasses[i].processSampleModulated(signal);
        }

        // 7. Split to stereo and process per-channel allpass chains
        float left = signal;
        float right = signal;

        for (int i = 0; i < kNumChannelAllpasses; ++i)
        {
            // Left channel LFO
            float lfoL = std::sin(leftLfoPhases[i]) * modDepthSamples;
            leftLfoPhases[i] += lfoPhaseInc;
            if (leftLfoPhases[i] >= juce::MathConstants<float>::twoPi)
                leftLfoPhases[i] -= juce::MathConstants<float>::twoPi;

            leftAllpasses[i].setModOffset(isFrozen ? 0.0f : lfoL);
            left = leftAllpasses[i].processSampleModulated(left);

            // Right channel LFO
            float lfoR = std::sin(rightLfoPhases[i]) * modDepthSamples;
            rightLfoPhases[i] += lfoPhaseInc;
            if (rightLfoPhases[i] >= juce::MathConstants<float>::twoPi)
                rightLfoPhases[i] -= juce::MathConstants<float>::twoPi;

            rightAllpasses[i].setModOffset(isFrozen ? 0.0f : lfoR);
            right = rightAllpasses[i].processSampleModulated(right);
        }

        // 8. Apply shelving EQ (Lo/Hi) to the output
        left = loShelfL.processSample(left);
        left = hiShelfL.processSample(left);
        right = loShelfR.processSample(right);
        right = hiShelfR.processSample(right);

        // 9. Store for feedback on next sample
        prevFeedbackL = left;
        prevFeedbackR = right;

        // 10. Write to output buffer (wet signal only)
        leftData[n] = left;
        rightData[n] = right;
    }
}

void ReverbEngine::reset()
{
    // Reset all allpasses
    for (int i = 0; i < kNumSharedAllpasses; ++i)
        sharedAllpasses[i].reset();

    for (int i = 0; i < kNumChannelAllpasses; ++i)
    {
        leftAllpasses[i].reset();
        rightAllpasses[i].reset();
    }

    // Zero pre-delay buffer
    std::fill(preDelayBuffer.begin(), preDelayBuffer.end(), 0.0f);
    preDelayWritePos = 0;

    // Reset all filters
    loShelfL.reset();
    loShelfR.reset();
    hiShelfL.reset();
    hiShelfR.reset();

    // Zero feedback
    prevFeedbackL = 0.0f;
    prevFeedbackR = 0.0f;

    // Reset LFO phases to initial distribution
    for (int i = 0; i < kNumSharedAllpasses; ++i)
    {
        sharedLfoPhases[i] = (juce::MathConstants<float>::twoPi * i) / kNumSharedAllpasses;
    }

    for (int i = 0; i < kNumChannelAllpasses; ++i)
    {
        leftLfoPhases[i] = (juce::MathConstants<float>::twoPi * i) / kNumChannelAllpasses + 0.5f;
        rightLfoPhases[i] = (juce::MathConstants<float>::twoPi * i) / kNumChannelAllpasses + 1.3f;
    }
}
