#include "FilterUtils.h"

// HighPassFilter
void HighPassFilter::prepare(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    juce::dsp::ProcessSpec spec{sampleRate, static_cast<juce::uint32>(samplesPerBlock), 1};
    filter.prepare(spec);
    reset();
}

void HighPassFilter::setCutoff(float freqHz)
{
    filter.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass(
        currentSampleRate,
        juce::jlimit(20.0f, static_cast<float>(currentSampleRate * 0.49), freqHz)
    );
}

float HighPassFilter::processSample(float input)
{
    return filter.processSample(input);
}

void HighPassFilter::reset()
{
    filter.reset();
}

// LowPassFilter
void LowPassFilter::prepare(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    juce::dsp::ProcessSpec spec{sampleRate, static_cast<juce::uint32>(samplesPerBlock), 1};
    filter.prepare(spec);
    reset();
}

void LowPassFilter::setCutoff(float freqHz)
{
    filter.coefficients = juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass(
        currentSampleRate,
        juce::jlimit(20.0f, static_cast<float>(currentSampleRate * 0.49), freqHz)
    );
}

float LowPassFilter::processSample(float input)
{
    return filter.processSample(input);
}

void LowPassFilter::reset()
{
    filter.reset();
}

// AllPassDelay
AllPassDelay::AllPassDelay(int maxDelaySamples)
{
    bufferSize = juce::nextPowerOfTwo(maxDelaySamples);
    bufferMask = bufferSize - 1;
    buffer.resize(bufferSize, 0.0f);
}

void AllPassDelay::prepare(double sr)
{
    sampleRate = sr;
    reset();
}

void AllPassDelay::setDelay(float samples)
{
    delaySamples = juce::jlimit(1.0f, static_cast<float>(bufferSize - 4), samples);
}

void AllPassDelay::setCoefficient(float g)
{
    coefficient = juce::jlimit(-0.99f, 0.99f, g);
}

float AllPassDelay::processSample(float input)
{
    // Calculate read position
    float readPos = static_cast<float>(writePos) - delaySamples;
    if (readPos < 0.0f)
        readPos += bufferSize;

    // Linear interpolation
    int index0 = static_cast<int>(readPos);
    int index1 = (index0 + 1) & bufferMask;
    float frac = readPos - index0;

    float delayed = buffer[index0] + frac * (buffer[index1] - buffer[index0]);

    // All-pass: output = -g*input + delayed + g*delayedOutput
    float output = -coefficient * input + delayed + coefficient * delayedOutput;
    delayedOutput = output;

    // Write input to buffer
    buffer[writePos] = input;
    writePos = (writePos + 1) & bufferMask;

    return output;
}

void AllPassDelay::setModulation(float depthSamples, float rateHz, float phaseOffset)
{
    baseDelay = delaySamples;
    modDepth = depthSamples;
    lfoPhaseOffset = phaseOffset;
    lfoIncrement = (rateHz * juce::MathConstants<float>::twoPi) / static_cast<float>(sampleRate);
}

float AllPassDelay::processSampleModulated(float input)
{
    // Update LFO
    lfoPhase += lfoIncrement;
    if (lfoPhase >= juce::MathConstants<float>::twoPi)
        lfoPhase -= juce::MathConstants<float>::twoPi;

    // Calculate modulated delay
    float lfo = std::sin(lfoPhase + lfoPhaseOffset);
    float modulatedDelay = baseDelay + lfo * modDepth;
    modulatedDelay = juce::jlimit(1.0f, static_cast<float>(bufferSize - 4), modulatedDelay);

    // Same as processSample but with modulatedDelay
    float readPos = static_cast<float>(writePos) - modulatedDelay;
    if (readPos < 0.0f)
        readPos += bufferSize;

    int index0 = static_cast<int>(readPos);
    int index1 = (index0 + 1) & bufferMask;
    float frac = readPos - index0;

    float delayed = buffer[index0] + frac * (buffer[index1] - buffer[index0]);
    float output = -coefficient * input + delayed + coefficient * delayedOutput;
    delayedOutput = output;

    buffer[writePos] = input;
    writePos = (writePos + 1) & bufferMask;

    return output;
}

void AllPassDelay::reset()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
    delayedOutput = 0.0f;
    lfoPhase = 0.0f;
}
