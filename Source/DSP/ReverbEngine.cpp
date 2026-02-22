#include "ReverbEngine.h"

ReverbEngine::ReverbEngine()
{
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
    currentResonance = 0.0f;
    resonanceQ = 0.707f;

    for (int i = 0; i < kNumSharedAllpasses; ++i)
        sharedLfoPhases[i] = (juce::MathConstants<float>::twoPi * i) / kNumSharedAllpasses;

    for (int i = 0; i < kNumChannelAllpasses; ++i)
    {
        leftLfoPhases[i]  = (juce::MathConstants<float>::twoPi * i) / kNumChannelAllpasses;
        rightLfoPhases[i] = (juce::MathConstants<float>::twoPi * i) / kNumChannelAllpasses
                            + juce::MathConstants<float>::halfPi;
    }
}

void ReverbEngine::prepare(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    sampleRateScale = static_cast<float>(sampleRate / 44100.0);

    for (int i = 0; i < kNumSharedAllpasses; ++i)
    {
        int baseDelay = sharedDelays[i];
        int maxDelay = static_cast<int>(baseDelay * sampleRateScale * 1.3f);
        sharedAllpasses[i].init(maxDelay);
        sharedAllpasses[i].prepare(sampleRate);
        sharedAllpasses[i].setDelay(baseDelay * sampleRateScale * currentSize);
    }

    for (int i = 0; i < kNumChannelAllpasses; ++i)
    {
        int lBase = leftDelays[i];
        int lMax  = static_cast<int>(lBase * sampleRateScale * 1.3f);
        leftAllpasses[i].init(lMax);
        leftAllpasses[i].prepare(sampleRate);
        leftAllpasses[i].setDelay(lBase * sampleRateScale * currentSize);

        int rBase = rightDelays[i];
        int rMax  = static_cast<int>(rBase * sampleRateScale * 1.3f);
        rightAllpasses[i].init(rMax);
        rightAllpasses[i].prepare(sampleRate);
        rightAllpasses[i].setDelay(rBase * sampleRateScale * currentSize);
    }

    // Pre-delay buffer
    int preDelaySize = juce::nextPowerOfTwo(kMaxPreDelaySamples);
    preDelayBuffer.resize(preDelaySize, 0.0f);
    preDelayMask = preDelaySize - 1;
    preDelayWritePos = 0;

    juce::dsp::ProcessSpec spec{sampleRate, static_cast<juce::uint32>(samplesPerBlock), 1};

    // Feedback path filters
    feedbackDampingL.prepare(spec);
    feedbackDampingR.prepare(spec);
    feedbackHPL.prepare(spec);
    feedbackHPR.prepare(spec);
    resPeakLoL.prepare(spec);
    resPeakLoR.prepare(spec);
    resPeakHiL.prepare(spec);
    resPeakHiR.prepare(spec);
    feedbackLoShelfL.prepare(spec);
    feedbackLoShelfR.prepare(spec);
    feedbackHiShelfL.prepare(spec);
    feedbackHiShelfR.prepare(spec);

    // Output path filters
    outputLoShelfL.prepare(spec);
    outputLoShelfR.prepare(spec);
    outputHiShelfL.prepare(spec);
    outputHiShelfR.prepare(spec);

    // Feedback damping: LP at 10kHz (hi roll-off) + HP at 80Hz (lo roll-off)
    auto lpCoeffs = juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass(sampleRate, 10000.0f);
    feedbackDampingL.coefficients = lpCoeffs;
    feedbackDampingR.coefficients = lpCoeffs;

    auto hpCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 80.0f);
    feedbackHPL.coefficients = hpCoeffs;
    feedbackHPR.coefficients = hpCoeffs;

    // Initialize EQ and resonance to flat
    resonanceQ = 0.707f;
    currentResonance = 0.0f;
    setLoEQ(0.0f);
    setHiEQ(0.0f);
    setResonance(0.0f);

    reset();
}

void ReverbEngine::setGravity(float gravity)
{
    float g, decay;
    if (gravity >= 0.0f)
    {
        g     = juce::jmap(gravity, 0.0f, 100.0f, 0.6f, 0.7f);
        decay = juce::jmap(gravity, 0.0f, 100.0f, 0.99997f, 0.999995f);
    }
    else
    {
        g     = juce::jmap(gravity, -100.0f, 0.0f, -0.7f, 0.6f);
        decay = juce::jmap(gravity, -100.0f, 0.0f, 0.99990f, 0.99997f);
    }

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
    float scaleFactor = juce::jlimit(0.05f, 1.3f, size / 100.0f);
    currentSize = scaleFactor;

    for (int i = 0; i < kNumSharedAllpasses; ++i)
        sharedAllpasses[i].setDelay(sharedDelays[i] * sampleRateScale * currentSize);

    for (int i = 0; i < kNumChannelAllpasses; ++i)
    {
        leftAllpasses[i].setDelay(leftDelays[i]   * sampleRateScale * currentSize);
        rightAllpasses[i].setDelay(rightDelays[i] * sampleRateScale * currentSize);
    }
}

void ReverbEngine::setPreDelay(float ms)
{
    preDelaySamples = juce::jlimit(0.0f, static_cast<float>(kMaxPreDelaySamples - 1),
                                   (ms / 1000.0f) * static_cast<float>(currentSampleRate));
}

void ReverbEngine::setFeedback(float percent)
{
    feedbackAmount = juce::jlimit(0.0f, 0.85f, (percent / 100.0f) * 0.85f);
    updateResonancePeaks();
}

void ReverbEngine::setModulation(float depthPercent, float rateHz)
{
    modDepthSamples = juce::jmap(depthPercent, 0.0f, 100.0f, 0.0f, 12.0f);
    lfoPhaseInc = (rateHz * juce::MathConstants<float>::twoPi) / static_cast<float>(currentSampleRate);
}

void ReverbEngine::setLoEQ(float dB)
{
    currentLoEQdB = dB;

    // Feedback path: cut only (std::min guarantees gain <= 0 dB in the loop)
    float feedbackGain = juce::Decibels::decibelsToGain(std::min(dB, 0.0f));
    auto fbCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
        currentSampleRate, 350.0f, resonanceQ, feedbackGain);
    feedbackLoShelfL.coefficients = fbCoeffs;
    feedbackLoShelfR.coefficients = fbCoeffs;

    // Output path: boost only (std::max guarantees gain >= 0 dB, outside the loop)
    float outputGain = juce::Decibels::decibelsToGain(std::max(dB, 0.0f));
    auto outCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
        currentSampleRate, 350.0f, resonanceQ, outputGain);
    outputLoShelfL.coefficients = outCoeffs;
    outputLoShelfR.coefficients = outCoeffs;

    // Resonance peak gain at 350 Hz must be scaled back when Lo EQ is cutting
    updateResonancePeaks();
}

void ReverbEngine::setHiEQ(float dB)
{
    currentHiEQdB = dB;

    // Feedback path: cut only
    float feedbackGain = juce::Decibels::decibelsToGain(std::min(dB, 0.0f));
    auto fbCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        currentSampleRate, 2000.0f, resonanceQ, feedbackGain);
    feedbackHiShelfL.coefficients = fbCoeffs;
    feedbackHiShelfR.coefficients = fbCoeffs;

    // Output path: boost only
    float outputGain = juce::Decibels::decibelsToGain(std::max(dB, 0.0f));
    auto outCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        currentSampleRate, 2000.0f, resonanceQ, outputGain);
    outputHiShelfL.coefficients = outCoeffs;
    outputHiShelfR.coefficients = outCoeffs;

    // Resonance peak gain at 2000 Hz must be scaled back when Hi EQ is cutting
    updateResonancePeaks();
}

void ReverbEngine::setResonance(float percent)
{
    currentResonance = percent;
    // Update shelving Q first so the shelf coefficients use the right Q when
    // setLoEQ/setHiEQ are called below. Those calls also invoke updateResonancePeaks,
    // so we don't need to call it separately here.
    resonanceQ = juce::jmap(percent, 0.0f, 100.0f, 0.707f, 2.0f);
    setLoEQ(currentLoEQdB);   // Updates shelves + peaks
    setHiEQ(currentHiEQdB);   // Updates shelves + peaks
}

void ReverbEngine::updateResonancePeaks()
{
    if (currentResonance < 0.5f)
    {
        // Resonance off — unity gain peaks
        auto flatLo = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
            currentSampleRate, 350.0f, 1.0f, 1.0f);
        resPeakLoL.coefficients = flatLo;
        resPeakLoR.coefficients = flatLo;

        auto flatHi = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
            currentSampleRate, 2000.0f, 1.0f, 1.0f);
        resPeakHiL.coefficients = flatHi;
        resPeakHiR.coefficients = flatHi;
        return;
    }

    float q = juce::jmap(currentResonance, 0.0f, 100.0f, 0.5f, 6.0f);

    // Base max gain scales inversely with feedback (prevents loop compounding)
    float maxGainDB = juce::jmap(feedbackAmount, 0.0f, 0.85f, 6.0f, 1.5f);

    // EQ penalty: when a shelf is cutting, reduce the corresponding peak gain.
    // At -12 dB cut the penalty is 0.0 (peak disabled), at 0 dB it is 1.0 (no penalty).
    float loEQPenalty = (currentLoEQdB < 0.0f)
        ? juce::jlimit(0.0f, 1.0f, juce::jmap(currentLoEQdB, -12.0f, 0.0f, 0.0f, 1.0f))
        : 1.0f;
    float hiEQPenalty = (currentHiEQdB < 0.0f)
        ? juce::jlimit(0.0f, 1.0f, juce::jmap(currentHiEQdB, -12.0f, 0.0f, 0.0f, 1.0f))
        : 1.0f;

    float loGainDB = juce::jmap(currentResonance, 0.0f, 100.0f, 0.0f, maxGainDB) * loEQPenalty;
    float hiGainDB = juce::jmap(currentResonance, 0.0f, 100.0f, 0.0f, maxGainDB) * hiEQPenalty;

    auto loCoeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        currentSampleRate, 350.0f, q, juce::Decibels::decibelsToGain(loGainDB));
    resPeakLoL.coefficients = loCoeffs;
    resPeakLoR.coefficients = loCoeffs;

    auto hiCoeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        currentSampleRate, 2000.0f, q, juce::Decibels::decibelsToGain(hiGainDB));
    resPeakHiL.coefficients = hiCoeffs;
    resPeakHiR.coefficients = hiCoeffs;
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
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels == 0)
        return;

    auto* leftData  = buffer.getWritePointer(0);
    auto* rightData = numChannels > 1 ? buffer.getWritePointer(1) : leftData;

    for (int n = 0; n < numSamples; ++n)
    {
        // 1. Sum stereo input to mono
        float monoIn = (leftData[n] + rightData[n]) * 0.5f;

        // 2. Build feedback signal by running prev output through damping chain
        float actualFeedback = feedbackAmount;
        float feedbackL, feedbackR;

        if (isFrozen)
        {
            // Freeze: very high feedback, bypass all damping
            actualFeedback = 0.995f;
            feedbackL = prevFeedbackL;
            feedbackR = prevFeedbackR;
        }
        else
        {
            // Normal: full damping chain
            // Band-limiting: LP at 10kHz (hi roll-off) + HP at 80Hz (lo roll-off)
            feedbackL = feedbackDampingL.processSample(prevFeedbackL);
            feedbackL = feedbackHPL.processSample(feedbackL);
            // Resonance peaks boost selected frequencies in the loop (causes them to ring longer)
            feedbackL = resPeakLoL.processSample(feedbackL);
            feedbackL = resPeakHiL.processSample(feedbackL);
            // Cut-only EQ shelves (user Lo/Hi EQ, negative dB only)
            feedbackL = feedbackLoShelfL.processSample(feedbackL);
            feedbackL = feedbackHiShelfL.processSample(feedbackL);

            feedbackR = feedbackDampingR.processSample(prevFeedbackR);
            feedbackR = feedbackHPR.processSample(feedbackR);
            feedbackR = resPeakLoR.processSample(feedbackR);
            feedbackR = resPeakHiR.processSample(feedbackR);
            feedbackR = feedbackLoShelfR.processSample(feedbackR);
            feedbackR = feedbackHiShelfR.processSample(feedbackR);
        }

        // 3. Freeze kills new input
        if (isFrozen)
            monoIn = 0.0f;

        // 4. Inject damped feedback (average L+R to keep it mono before the allpass chain)
        monoIn += (feedbackL + feedbackR) * 0.5f * actualFeedback;

        // 5. Soft-clip before the allpass chain
        monoIn = std::tanh(monoIn);

        // 6. Pre-delay
        preDelayBuffer[preDelayWritePos & preDelayMask] = monoIn;
        int delayOffset = static_cast<int>(preDelaySamples);
        int readIdx = (preDelayWritePos - delayOffset + static_cast<int>(preDelayBuffer.size())) & preDelayMask;
        monoIn = preDelayBuffer[readIdx];
        preDelayWritePos = (preDelayWritePos + 1) & preDelayMask;

        // 7. Shared allpass chain (mono)
        float signal = monoIn;
        for (int i = 0; i < kNumSharedAllpasses; ++i)
        {
            float lfo = std::sin(sharedLfoPhases[i]) * modDepthSamples;
            sharedLfoPhases[i] += lfoPhaseInc;
            if (sharedLfoPhases[i] >= juce::MathConstants<float>::twoPi)
                sharedLfoPhases[i] -= juce::MathConstants<float>::twoPi;

            sharedAllpasses[i].setModOffset(isFrozen ? 0.0f : lfo);
            signal = sharedAllpasses[i].processSampleModulated(signal);
        }

        // 8. Per-channel allpass chains (stereo split)
        float left  = signal;
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

        // 9. Store output for next feedback iteration BEFORE output EQ
        //    (output EQ boost should not re-enter the feedback loop)
        prevFeedbackL = left;
        prevFeedbackR = right;

        // 10. Output EQ (boost only — safe outside feedback loop)
        left  = outputLoShelfL.processSample(left);
        left  = outputHiShelfL.processSample(left);
        right = outputLoShelfR.processSample(right);
        right = outputHiShelfR.processSample(right);

        // 11. Safety clamp + NaN protection
        left  = std::clamp(left,  -4.0f, 4.0f);
        right = std::clamp(right, -4.0f, 4.0f);

        if (std::isnan(left)  || std::isinf(left))  left  = 0.0f;
        if (std::isnan(right) || std::isinf(right)) right = 0.0f;

        left  += 1e-25f;  // denormal prevention
        right += 1e-25f;

        // 12. Write output
        leftData[n]  = left;
        rightData[n] = right;
    }
}

void ReverbEngine::reset()
{
    for (int i = 0; i < kNumSharedAllpasses; ++i)
        sharedAllpasses[i].reset();
    for (int i = 0; i < kNumChannelAllpasses; ++i)
    {
        leftAllpasses[i].reset();
        rightAllpasses[i].reset();
    }

    std::fill(preDelayBuffer.begin(), preDelayBuffer.end(), 0.0f);
    preDelayWritePos = 0;

    // Reset feedback path filters
    feedbackDampingL.reset();
    feedbackDampingR.reset();
    feedbackHPL.reset();
    feedbackHPR.reset();
    resPeakLoL.reset();
    resPeakLoR.reset();
    resPeakHiL.reset();
    resPeakHiR.reset();
    feedbackLoShelfL.reset();
    feedbackLoShelfR.reset();
    feedbackHiShelfL.reset();
    feedbackHiShelfR.reset();

    // Reset output path filters
    outputLoShelfL.reset();
    outputLoShelfR.reset();
    outputHiShelfL.reset();
    outputHiShelfR.reset();

    prevFeedbackL = 0.0f;
    prevFeedbackR = 0.0f;

    for (int i = 0; i < kNumSharedAllpasses; ++i)
        sharedLfoPhases[i] = (juce::MathConstants<float>::twoPi * i) / kNumSharedAllpasses;

    for (int i = 0; i < kNumChannelAllpasses; ++i)
    {
        leftLfoPhases[i]  = (juce::MathConstants<float>::twoPi * i) / kNumChannelAllpasses;
        rightLfoPhases[i] = (juce::MathConstants<float>::twoPi * i) / kNumChannelAllpasses
                            + juce::MathConstants<float>::halfPi;
    }
}
