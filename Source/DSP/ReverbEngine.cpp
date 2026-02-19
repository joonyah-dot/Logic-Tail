#include "ReverbEngine.h"

ReverbEngine::ReverbEngine()
{
    // Initialize all state variables to safe defaults
    prevFeedbackL = 0.0f;
    prevFeedbackR = 0.0f;
    feedbackAmount = 0.0f;
    modDepthSamples = 0.0f;
    lfoPhaseInc = 0.0f;
    preDelaySamples = 0.0f;
    preDelayWritePos = 0;
    isFrozen = false;
    killDrySignal = false;
    currentLoEQdB = 0.0f;
    currentHiEQdB = 0.0f;
    resonanceQ = 0.707f;

    // Initialize LFO phases with even distribution to decorrelate modulation
    for (int i = 0; i < kNumSharedAllpasses; ++i)
        sharedLfoPhases[i] = (juce::MathConstants<float>::twoPi * i) / kNumSharedAllpasses;

    for (int i = 0; i < kNumChannelAllpasses; ++i)
    {
        leftLfoPhases[i]  = (juce::MathConstants<float>::twoPi * i) / kNumChannelAllpasses;
        // 90° offset from left for decorrelation
        rightLfoPhases[i] = (juce::MathConstants<float>::twoPi * i) / kNumChannelAllpasses
                            + juce::MathConstants<float>::halfPi;
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

    // Prepare shelving EQ filters (feedback path + output path)
    juce::dsp::ProcessSpec spec{sampleRate, static_cast<juce::uint32>(samplesPerBlock), 1};
    loShelfL.prepare(spec);
    loShelfR.prepare(spec);
    hiShelfL.prepare(spec);
    hiShelfR.prepare(spec);
    outputLoShelfL.prepare(spec);
    outputLoShelfR.prepare(spec);
    outputHiShelfL.prepare(spec);
    outputHiShelfR.prepare(spec);

    // Prepare dedicated feedback damping filters (always-on decay mechanism)
    feedbackDampingL.prepare(spec);
    feedbackDampingR.prepare(spec);

    // Set damping filters to 12kHz lowpass (gentle high-frequency decay)
    auto dampingCoeffs = juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass(sampleRate, 12000.0f);
    feedbackDampingL.coefficients = dampingCoeffs;
    feedbackDampingR.coefficients = dampingCoeffs;

    // Initialize EQ to flat (0 dB, unity)
    resonanceQ = 0.707f;
    setLoEQ(0.0f);
    setHiEQ(0.0f);

    reset();
}

void ReverbEngine::setGravity(float gravity)
{
    // Map gravity to allpass coefficient, per-allpass decay gain, and early tap scaling.
    float g;
    float decay;
    if (gravity >= 0.0f)
    {
        // Positive gravity: narrower coefficient range keeps reverb consistently dense
        g = juce::jmap(gravity, 0.0f, 100.0f, 0.6f, 0.7f);
        // Longer decay range: ~4-5s at gravity=0, ~10-15s at gravity=100
        decay = juce::jmap(gravity, 0.0f, 100.0f, 0.99997f, 0.999995f);
    }
    else
    {
        // Negative gravity: reverse/swell character, decays faster
        g = juce::jmap(gravity, -100.0f, 0.0f, -0.7f, 0.6f);
        decay = juce::jmap(gravity, -100.0f, 0.0f, 0.99990f, 0.99997f);
    }

    // Apply coefficient and decay gain to all allpasses
    for (int i = 0; i < kNumSharedAllpasses; ++i)
    {
        sharedAllpasses[i].setCoefficient(g);
        sharedAllpasses[i].setDecayGain(decay);
    }

    for (int i = 0; i < kNumChannelAllpasses; ++i)
    {
        leftAllpasses[i].setCoefficient(g);
        leftAllpasses[i].setDecayGain(decay);
        rightAllpasses[i].setCoefficient(g);
        rightAllpasses[i].setDecayGain(decay);
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
    // Scale 0-100% → 0-0.85. The 12kHz damping filter in the feedback path
    // prevents runaway even at high feedback values.
    feedbackAmount = (percent / 100.0f) * 0.85f;
    feedbackAmount = juce::jlimit(0.0f, 0.85f, feedbackAmount);
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
    currentLoEQdB = dB;

    auto unityCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
        currentSampleRate, 350.0f, resonanceQ, 1.0f);
    auto shapeCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
        currentSampleRate, 350.0f, resonanceQ, juce::Decibels::decibelsToGain(dB));

    if (dB <= 0.0f)
    {
        // Cut: apply in feedback path to shape decay, output stays flat
        loShelfL.coefficients = shapeCoeffs;
        loShelfR.coefficients = shapeCoeffs;
        outputLoShelfL.coefficients = unityCoeffs;
        outputLoShelfR.coefficients = unityCoeffs;
    }
    else
    {
        // Boost: feedback path stays flat, apply boost at output only
        loShelfL.coefficients = unityCoeffs;
        loShelfR.coefficients = unityCoeffs;
        outputLoShelfL.coefficients = shapeCoeffs;
        outputLoShelfR.coefficients = shapeCoeffs;
    }
}

void ReverbEngine::setHiEQ(float dB)
{
    currentHiEQdB = dB;

    auto unityCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        currentSampleRate, 2000.0f, resonanceQ, 1.0f);
    auto shapeCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        currentSampleRate, 2000.0f, resonanceQ, juce::Decibels::decibelsToGain(dB));

    if (dB <= 0.0f)
    {
        // Cut: apply in feedback path to shape decay, output stays flat
        hiShelfL.coefficients = shapeCoeffs;
        hiShelfR.coefficients = shapeCoeffs;
        outputHiShelfL.coefficients = unityCoeffs;
        outputHiShelfR.coefficients = unityCoeffs;
    }
    else
    {
        // Boost: feedback path stays flat, apply boost at output only
        hiShelfL.coefficients = unityCoeffs;
        hiShelfR.coefficients = unityCoeffs;
        outputHiShelfL.coefficients = shapeCoeffs;
        outputHiShelfR.coefficients = shapeCoeffs;
    }
}

void ReverbEngine::setResonance(float percent)
{
    resonanceQ = juce::jmap(percent, 0.0f, 100.0f, 0.707f, 4.0f);
    // Re-apply EQ so shelving filters use the new Q
    setLoEQ(currentLoEQdB);
    setHiEQ(currentHiEQdB);
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

        // 2. Process feedback through damping filters BEFORE adding to input
        float feedbackL = prevFeedbackL;
        float feedbackR = prevFeedbackR;

        // Determine actual feedback coefficient
        float actualFeedback = feedbackAmount;
        if (isFrozen)
        {
            // Freeze mode: very high feedback for near-infinite sustain
            actualFeedback = 0.995f;
            // Skip damping - feedback passes through unchanged
        }
        else
        {
            // Normal mode: apply full damping chain to feedback
            feedbackL = feedbackDampingL.processSample(feedbackL);
            feedbackL = loShelfL.processSample(feedbackL);
            feedbackL = hiShelfL.processSample(feedbackL);
            feedbackR = feedbackDampingR.processSample(feedbackR);
            feedbackR = loShelfR.processSample(feedbackR);
            feedbackR = hiShelfR.processSample(feedbackR);
        }

        // 3. If freeze is active, kill new input
        if (isFrozen)
            monoIn = 0.0f;

        // 4. Add damped feedback from previous iteration (average of L/R)
        monoIn += (feedbackL + feedbackR) * 0.5f * actualFeedback;

        // 5. Soft-clip to prevent blowup at high feedback
        monoIn = std::tanh(monoIn);

        // 6. Pre-delay with safe buffer access
        int safeWritePos = preDelayWritePos & preDelayMask;
        preDelayBuffer[safeWritePos] = monoIn;

        // Calculate read position with proper wrapping to handle negative values
        int delayOffset = static_cast<int>(preDelaySamples);
        int readIdx = (preDelayWritePos - delayOffset + static_cast<int>(preDelayBuffer.size())) & preDelayMask;
        monoIn = preDelayBuffer[readIdx];

        preDelayWritePos = (preDelayWritePos + 1) & preDelayMask;

        // 7. Process through shared allpass chain (mono, in series)
        float signal = monoIn;
        for (int i = 0; i < kNumSharedAllpasses; ++i)
        {
            float lfoValue = std::sin(sharedLfoPhases[i]) * modDepthSamples;
            sharedLfoPhases[i] += lfoPhaseInc;
            if (sharedLfoPhases[i] >= juce::MathConstants<float>::twoPi)
                sharedLfoPhases[i] -= juce::MathConstants<float>::twoPi;

            sharedAllpasses[i].setModOffset(isFrozen ? 0.0f : lfoValue);
            signal = sharedAllpasses[i].processSampleModulated(signal);
        }

        // 8. Split to stereo and process per-channel allpass chains
        float left = signal;
        float right = signal;

        for (int i = 0; i < kNumChannelAllpasses; ++i)
        {
            float lfoL = std::sin(leftLfoPhases[i]) * modDepthSamples;
            leftLfoPhases[i] += lfoPhaseInc;
            if (leftLfoPhases[i] >= juce::MathConstants<float>::twoPi)
                leftLfoPhases[i] -= juce::MathConstants<float>::twoPi;

            leftAllpasses[i].setModOffset(isFrozen ? 0.0f : lfoL);
            left = leftAllpasses[i].processSampleModulated(left);

            float lfoR = std::sin(rightLfoPhases[i]) * modDepthSamples;
            rightLfoPhases[i] += lfoPhaseInc;
            if (rightLfoPhases[i] >= juce::MathConstants<float>::twoPi)
                rightLfoPhases[i] -= juce::MathConstants<float>::twoPi;

            rightAllpasses[i].setModOffset(isFrozen ? 0.0f : lfoR);
            right = rightAllpasses[i].processSampleModulated(right);
        }

        // 9. Apply output EQ (boost only — safe outside feedback loop)
        left  = outputLoShelfL.processSample(left);
        left  = outputHiShelfL.processSample(left);
        right = outputLoShelfR.processSample(right);
        right = outputHiShelfR.processSample(right);

        // 10. Hard clipping safety to prevent ANY runaway conditions
        left = std::clamp(left, -4.0f, 4.0f);
        right = std::clamp(right, -4.0f, 4.0f);

        // 11. NaN and denormal protection
        if (std::isnan(left) || std::isinf(left))
            left = 0.0f;
        if (std::isnan(right) || std::isinf(right))
            right = 0.0f;

        // Add tiny DC offset to prevent denormals
        left += 1e-25f;
        right += 1e-25f;

        // 12. Store RAW output for feedback on next sample
        // The damping filters will be applied to this BEFORE adding to input
        prevFeedbackL = left;
        prevFeedbackR = right;

        // 13. Write to output buffer (wet signal only)
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
    outputLoShelfL.reset();
    outputLoShelfR.reset();
    outputHiShelfL.reset();
    outputHiShelfR.reset();
    feedbackDampingL.reset();
    feedbackDampingR.reset();

    // Zero feedback
    prevFeedbackL = 0.0f;
    prevFeedbackR = 0.0f;

    // Reset LFO phases to initial distribution
    for (int i = 0; i < kNumSharedAllpasses; ++i)
        sharedLfoPhases[i] = (juce::MathConstants<float>::twoPi * i) / kNumSharedAllpasses;

    for (int i = 0; i < kNumChannelAllpasses; ++i)
    {
        leftLfoPhases[i]  = (juce::MathConstants<float>::twoPi * i) / kNumChannelAllpasses;
        rightLfoPhases[i] = (juce::MathConstants<float>::twoPi * i) / kNumChannelAllpasses
                            + juce::MathConstants<float>::halfPi;
    }
}
