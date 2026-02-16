#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class LogicTailAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit LogicTailAudioProcessorEditor (LogicTailAudioProcessor&);
    ~LogicTailAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    LogicTailAudioProcessor& processor;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LogicTailAudioProcessorEditor)
};
