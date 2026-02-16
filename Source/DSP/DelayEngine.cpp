#include "DelayEngine.h"

void DelayEngine::prepare(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    // Allocate for 2 seconds max delay (rounded to power of 2)
    int maxDelaySamples = static_cast<int>(sampleRate * 2.0);
    int bufferSize = juce::nextPowerOfTwo(maxDelaySamples);

    delayBufferL.resize(bufferSize, 0.0f);
    delayBufferR.resize(bufferSize, 0.0f);
    bufferMask = bufferSize - 1;

    writePos = 0;

    // Prepare filters
    highPassL.prepare(sampleRate, samplesPerBlock);
    highPassR.prepare(sampleRate, samplesPerBlock);
    lowPassL.prepare(sampleRate, samplesPerBlock);
    lowPassR.prepare(sampleRate, samplesPerBlock);
}

void DelayEngine::setDelayTime(float timeMs)
{
    delaySamples = (timeMs / 1000.0f) * static_cast<float>(currentSampleRate);
    delaySamples = juce::jlimit(1.0f, static_cast<float>(delayBufferL.size() - 4), delaySamples);
}

void DelayEngine::setFeedback(float feedbackPercent)
{
    feedbackAmount = juce::jlimit(0.0f, 0.95f, feedbackPercent / 100.0f);
}

void DelayEngine::setHighPassFreq(float hz)
{
    highPassL.setCutoff(hz);
    highPassR.setCutoff(hz);
}

void DelayEngine::setLowPassFreq(float hz)
{
    lowPassL.setCutoff(hz);
    lowPassR.setCutoff(hz);
}

float DelayEngine::cubicHermite(float y0, float y1, float y2, float y3, float frac)
{
    float c0 = y1;
    float c1 = 0.5f * (y2 - y0);
    float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

void DelayEngine::process(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels == 0)
        return;

    auto* leftData = buffer.getWritePointer(0);
    auto* rightData = numChannels > 1 ? buffer.getWritePointer(1) : leftData;

    for (int i = 0; i < numSamples; ++i)
    {
        // Calculate read position
        float readPos = static_cast<float>(writePos) - delaySamples;
        if (readPos < 0.0f)
            readPos += delayBufferL.size();

        // Get 4 samples for cubic interpolation
        int idx = static_cast<int>(readPos);
        float frac = readPos - idx;

        int i0 = (idx - 1) & bufferMask;
        int i1 = idx & bufferMask;
        int i2 = (idx + 1) & bufferMask;
        int i3 = (idx + 2) & bufferMask;

        // Read delayed samples with cubic interpolation
        float delayedL = cubicHermite(
            delayBufferL[i0], delayBufferL[i1], delayBufferL[i2], delayBufferL[i3], frac
        );
        float delayedR = cubicHermite(
            delayBufferR[i0], delayBufferR[i1], delayBufferR[i2], delayBufferR[i3], frac
        );

        // Apply filters in feedback path
        delayedL = highPassL.processSample(delayedL);
        delayedL = lowPassL.processSample(delayedL);
        delayedR = highPassR.processSample(delayedR);
        delayedR = lowPassR.processSample(delayedR);

        // Write input + filtered feedback to delay buffer
        delayBufferL[writePos] = leftData[i] + delayedL * feedbackAmount;
        delayBufferR[writePos] = rightData[i] + delayedR * feedbackAmount;

        // Output delayed signal
        leftData[i] = delayedL;
        rightData[i] = delayedR;

        writePos = (writePos + 1) & bufferMask;
    }
}

void DelayEngine::reset()
{
    std::fill(delayBufferL.begin(), delayBufferL.end(), 0.0f);
    std::fill(delayBufferR.begin(), delayBufferR.end(), 0.0f);
    writePos = 0;

    highPassL.reset();
    highPassR.reset();
    lowPassL.reset();
    lowPassR.reset();
}
