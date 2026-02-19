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
    reverbEngine.prepare(sampleRate, samplesPerBlock);
}

void LogicTailAudioProcessor::releaseResources()
{
    delayEngine.reset();
    reverbEngine.reset();
}

bool LogicTailAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void LogicTailAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Read reverb parameters
    float gravity = apvts.getRawParameterValue(ParameterIDs::reverb_gravity)->load();
    float size = apvts.getRawParameterValue(ParameterIDs::reverb_size)->load();
    float preDelay = apvts.getRawParameterValue(ParameterIDs::reverb_predelay)->load();
    float revFeedback = apvts.getRawParameterValue(ParameterIDs::reverb_feedback)->load();
    float revModDepth = apvts.getRawParameterValue(ParameterIDs::reverb_mod_depth)->load();
    float revModRate = apvts.getRawParameterValue(ParameterIDs::reverb_mod_rate)->load();
    float loEQ = apvts.getRawParameterValue(ParameterIDs::reverb_lo)->load();
    float hiEQ = apvts.getRawParameterValue(ParameterIDs::reverb_hi)->load();
    float resonance = apvts.getRawParameterValue(ParameterIDs::reverb_resonance)->load();
    bool freeze = apvts.getRawParameterValue(ParameterIDs::reverb_freeze)->load() > 0.5f;
    bool killDry = apvts.getRawParameterValue(ParameterIDs::reverb_kill_dry)->load() > 0.5f;

    // Read delay parameters
    float delTime = apvts.getRawParameterValue(ParameterIDs::delay_time)->load();
    float delFeedback = apvts.getRawParameterValue(ParameterIDs::delay_feedback)->load();
    float delHP = apvts.getRawParameterValue(ParameterIDs::delay_hp)->load();
    float delLP = apvts.getRawParameterValue(ParameterIDs::delay_lp)->load();

    // Read global parameters
    int routingIdx = static_cast<int>(apvts.getRawParameterValue(ParameterIDs::routing_mode)->load());
    float balance = apvts.getRawParameterValue(ParameterIDs::parallel_balance)->load() / 100.0f;
    float mix = apvts.getRawParameterValue(ParameterIDs::global_mix)->load() / 100.0f;
    float inGain = juce::Decibels::decibelsToGain(apvts.getRawParameterValue(ParameterIDs::input_gain)->load());
    float outGain = juce::Decibels::decibelsToGain(apvts.getRawParameterValue(ParameterIDs::output_gain)->load());

    // Update reverb engine
    reverbEngine.setGravity(gravity);
    reverbEngine.setSize(size);
    reverbEngine.setPreDelay(preDelay);
    reverbEngine.setFeedback(revFeedback);
    reverbEngine.setModulation(revModDepth, revModRate);
    reverbEngine.setLoEQ(loEQ);
    reverbEngine.setHiEQ(hiEQ);
    reverbEngine.setResonance(resonance);
    reverbEngine.setFreeze(freeze);
    reverbEngine.setKillDry(killDry);

    // Update delay engine
    delayEngine.setDelayTime(delTime);
    delayEngine.setFeedback(delFeedback);
    delayEngine.setHighPassFreq(delHP);
    delayEngine.setLowPassFreq(delLP);

    // Apply input gain
    buffer.applyGain(inGain);

    // Store dry signal
    juce::AudioBuffer<float> dryBuffer;
    dryBuffer.makeCopyOf(buffer);

    // --- ROUTING ---
    if (routingIdx == 0)
    {
        // Series: Delay → Reverb
        delayEngine.process(buffer);
        reverbEngine.process(buffer);
    }
    else if (routingIdx == 1)
    {
        // Series: Reverb → Delay
        reverbEngine.process(buffer);
        delayEngine.process(buffer);
    }
    else
    {
        // Parallel — skip unused engine when balance is at an extreme
        if (balance >= 0.99f)
        {
            // 100% reverb — skip delay entirely
            reverbEngine.process(buffer);
        }
        else if (balance <= 0.01f)
        {
            // 100% delay — skip reverb entirely
            delayEngine.process(buffer);
        }
        else
        {
            // Blend both engines
            juce::AudioBuffer<float> reverbBuffer;
            reverbBuffer.makeCopyOf(buffer);

            delayEngine.process(buffer);        // buffer now = delay wet
            reverbEngine.process(reverbBuffer); // reverbBuffer = reverb wet

            // Blend: balance 0 = all delay, 100 = all reverb
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                auto* delData = buffer.getWritePointer(ch);
                const auto* revData = reverbBuffer.getReadPointer(ch);
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    delData[i] = delData[i] * (1.0f - balance) + revData[i] * balance;
                }
            }
        }
    }

    // --- DRY/WET MIX ---
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* wet = buffer.getWritePointer(ch);
        const auto* dry = dryBuffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (killDry)
                wet[i] = wet[i];  // 100% wet when kill dry is on
            else
                wet[i] = dry[i] * (1.0f - mix) + wet[i] * mix;
        }
    }

    // Apply output gain
    buffer.applyGain(outGain);
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
