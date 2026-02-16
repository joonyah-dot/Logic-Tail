#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Utility/ParameterLayout.h"

LogicTailAudioProcessor::LogicTailAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    , apvts (*this, nullptr, "Parameters", createParameterLayout())
{
}

void LogicTailAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    delayEngine.prepare(sampleRate, samplesPerBlock);
}

void LogicTailAudioProcessor::releaseResources()
{
    delayEngine.reset();
}

bool LogicTailAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void LogicTailAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Read parameters
    float inputGainDb = apvts.getRawParameterValue(ParameterIDs::input_gain)->load();
    float outputGainDb = apvts.getRawParameterValue(ParameterIDs::output_gain)->load();
    float delayTimeMs = apvts.getRawParameterValue(ParameterIDs::delay_time)->load();
    float feedbackPercent = apvts.getRawParameterValue(ParameterIDs::delay_feedback)->load();
    float hpFreq = apvts.getRawParameterValue(ParameterIDs::delay_hp)->load();
    float lpFreq = apvts.getRawParameterValue(ParameterIDs::delay_lp)->load();
    float globalMixPercent = apvts.getRawParameterValue(ParameterIDs::global_mix)->load();

    // Convert to linear values
    float inputGain = juce::Decibels::decibelsToGain(inputGainDb);
    float outputGain = juce::Decibels::decibelsToGain(outputGainDb);
    float wetMix = globalMixPercent / 100.0f;
    float dryMix = 1.0f - wetMix;

    // Apply input gain
    buffer.applyGain(inputGain);

    // Store dry signal
    juce::AudioBuffer<float> dryBuffer;
    dryBuffer.makeCopyOf(buffer);

    // Configure delay engine
    delayEngine.setDelayTime(delayTimeMs);
    delayEngine.setFeedback(feedbackPercent);
    delayEngine.setHighPassFreq(hpFreq);
    delayEngine.setLowPassFreq(lpFreq);

    // Process wet signal
    delayEngine.process(buffer);

    // Mix dry/wet
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* wetData = buffer.getWritePointer(ch);
        const auto* dryData = dryBuffer.getReadPointer(ch);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            wetData[i] = dryData[i] * dryMix + wetData[i] * wetMix;
        }
    }

    // Apply output gain
    buffer.applyGain(outputGain);
}

juce::AudioProcessorEditor* LogicTailAudioProcessor::createEditor()
{
    return new LogicTailAudioProcessorEditor (*this);
}

void LogicTailAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void LogicTailAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}
