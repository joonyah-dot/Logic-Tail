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
    init(maxDelaySamples);
}

void AllPassDelay::init(int maxDelaySamples)
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
    // Split delay into integer and fractional parts
    int delayInt = static_cast<int>(delaySamples);
    float frac = delaySamples - static_cast<float>(delayInt);

    // Calculate read positions with proper wrapping
    int readPos0 = (writePos - delayInt + static_cast<int>(bufferSize)) & bufferMask;
    int readPos1 = (writePos - delayInt - 1 + static_cast<int>(bufferSize)) & bufferMask;

    // Linear interpolation with safe indices — reads v[n-D]
    float delayed = buffer[readPos0] + frac * (buffer[readPos1] - buffer[readPos0]);

    // Schroeder allpass:
    //   v[n]   = input + g * v[n-D]     (state variable stored in buffer)
    //   y[n]   = v[n-D] - g * v[n]      (energy-preserving, truly unity gain)
    float v = input + coefficient * delayed;
    float output = delayed - coefficient * v;

    // Write state variable v (NOT input) to buffer — this is critical for correct allpass behavior
    buffer[writePos & bufferMask] = v;
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

void AllPassDelay::setModOffset(float offsetSamples)
{
    modOffset = offsetSamples;
}

float AllPassDelay::processSampleModulated(float input)
{
    // Clamp total delay to valid range
    float totalDelay = juce::jlimit(1.0f, static_cast<float>(bufferSize - 2), delaySamples + modOffset);

    // Split into integer and fractional parts
    int delayInt = static_cast<int>(totalDelay);
    float frac = totalDelay - static_cast<float>(delayInt);

    // Calculate read positions — add bufferSize before masking to handle negative values
    int readPos0 = (writePos - delayInt + static_cast<int>(bufferSize)) & bufferMask;
    int readPos1 = (writePos - delayInt - 1 + static_cast<int>(bufferSize)) & bufferMask;

    // Linear interpolation — reads v[n-D]
    float delayed = buffer[readPos0] + frac * (buffer[readPos1] - buffer[readPos0]);

    // Schroeder allpass (identical formula to processSample):
    //   v[n]   = input + g * v[n-D]     (state variable stored in buffer)
    //   y[n]   = v[n-D] - g * v[n]      (energy-preserving, truly unity gain)
    float v = input + coefficient * delayed;
    float output = delayed - coefficient * v;

    // Write state variable v (NOT input) to buffer
    buffer[writePos & bufferMask] = v;
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
