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

    // Initialize smoothing and LFO state
    targetDelaySamples   = 0.0f;
    smoothedDelaySamples = 0.0f;
    modLfoPhaseL = 0.0f;
    modLfoPhaseR = 0.25f;
    modLfoInc    = 0.0f;
    modDepthSamples = 0.0f;
}

void DelayEngine::setDelayTime(float timeMs)
{
    targetDelaySamples = (timeMs / 1000.0f) * static_cast<float>(currentSampleRate);
    targetDelaySamples = juce::jlimit(1.0f,
        static_cast<float>(delayBufferL.size() - 4), targetDelaySamples);
}

void DelayEngine::setTempoSync(bool enabled, double bpm, int divisionIndex)
{
    tempoSyncEnabled = enabled;
    if (!enabled || bpm <= 0.0)
        return;

    // Multipliers relative to one quarter note, matching the 14-item StringArray in ParameterLayout:
    // "1/32","1/16T","1/16","1/16D","1/8T","1/8","1/8D","1/4T","1/4","1/4D","1/2T","1/2","1/2D","1/1"
    static const float divMults[] = {
        0.125f,        // 1/32
        1.0f / 6.0f,  // 1/16T
        0.25f,         // 1/16
        0.375f,        // 1/16D
        1.0f / 3.0f,  // 1/8T
        0.5f,          // 1/8
        0.75f,         // 1/8D
        2.0f / 3.0f,  // 1/4T
        1.0f,          // 1/4
        1.5f,          // 1/4D
        4.0f / 3.0f,  // 1/2T
        2.0f,          // 1/2
        3.0f,          // 1/2D
        4.0f           // 1/1
    };

    int idx = juce::jlimit(0, 13, divisionIndex);
    float beatsPerSecond = static_cast<float>(bpm) / 60.0f;
    float delayMs = (divMults[idx] / beatsPerSecond) * 1000.0f;
    setDelayTime(juce::jlimit(1.0f, 2000.0f, delayMs));
}

void DelayEngine::setPingPong(bool enabled)
{
    pingPongEnabled = enabled;
}

void DelayEngine::setModulation(float rateHz, float depthPercent)
{
    modLfoInc = rateHz / static_cast<float>(currentSampleRate);
    float maxDepthSamples = static_cast<float>(currentSampleRate) * 0.005f; // 5ms max
    modDepthSamples = maxDepthSamples * (depthPercent / 100.0f);
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
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numChannels == 0) return;

    auto* leftData  = buffer.getWritePointer(0);
    auto* rightData = numChannels > 1 ? buffer.getWritePointer(1) : leftData;

    for (int i = 0; i < numSamples; ++i)
    {
        // Smooth delay time (prevents clicks on tempo-sync jumps, τ ≈ 42ms @ 44100Hz)
        smoothedDelaySamples += (1.0f - kSmoothCoeff) * (targetDelaySamples - smoothedDelaySamples);

        // Per-channel LFO modulation offsets (normalized phase [0,1))
        float modOffsetL = modDepthSamples *
            std::sin(2.0f * juce::MathConstants<float>::pi * modLfoPhaseL);
        float modOffsetR = modDepthSamples *
            std::sin(2.0f * juce::MathConstants<float>::pi * modLfoPhaseR);

        modLfoPhaseL += modLfoInc;
        modLfoPhaseR += modLfoInc;
        if (modLfoPhaseL >= 1.0f) modLfoPhaseL -= 1.0f;
        if (modLfoPhaseR >= 1.0f) modLfoPhaseR -= 1.0f;

        float effectiveDelayL = juce::jlimit(1.0f,
            static_cast<float>(delayBufferL.size() - 4),
            smoothedDelaySamples + modOffsetL);
        float effectiveDelayR = juce::jlimit(1.0f,
            static_cast<float>(delayBufferR.size() - 4),
            smoothedDelaySamples + modOffsetR);

        // Read with cubic Hermite interpolation
        auto readWithInterp = [&](const std::vector<float>& buf, float delay) -> float {
            float readPos = static_cast<float>(writePos) - delay;
            if (readPos < 0.0f) readPos += static_cast<float>(buf.size());
            int rdIdx = static_cast<int>(readPos);
            float frac = readPos - static_cast<float>(rdIdx);
            return cubicHermite(
                buf[(rdIdx - 1) & bufferMask],
                buf[ rdIdx      & bufferMask],
                buf[(rdIdx + 1) & bufferMask],
                buf[(rdIdx + 2) & bufferMask],
                frac);
        };

        float delayedL = readWithInterp(delayBufferL, effectiveDelayL);
        float delayedR = readWithInterp(delayBufferR, effectiveDelayR);

        // Feedback path filters: HP then LP
        delayedL = lowPassL.processSample(highPassL.processSample(delayedL));
        delayedR = lowPassR.processSample(highPassR.processSample(delayedR));

        // Write input + feedback to delay buffer
        if (pingPongEnabled)
        {
            // Input is summed to mono and enters ONLY the L buffer.
            // R buffer receives ONLY the cross-fed feedback from L (no direct input).
            // This forces echoes to alternate strictly: L → R → L → R ...
            float monoIn = (leftData[i] + rightData[i]) * 0.5f;
            delayBufferL[writePos] = monoIn        + delayedR * feedbackAmount;
            delayBufferR[writePos] = delayedL      * feedbackAmount;
        }
        else
        {
            delayBufferL[writePos] = leftData[i]  + delayedL * feedbackAmount;
            delayBufferR[writePos] = rightData[i] + delayedR * feedbackAmount;
        }

        // Output is the delayed signal
        leftData[i]  = delayedL;
        rightData[i] = delayedR;

        writePos = (writePos + 1) & static_cast<int>(bufferMask);
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

    smoothedDelaySamples = 0.0f;
    modLfoPhaseL = 0.0f;
    modLfoPhaseR = 0.25f;
}
