#include "PluginEditor.h"

LogicTailAudioProcessorEditor::LogicTailAudioProcessorEditor (LogicTailAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (420, 260);
}

void LogicTailAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    g.setColour (juce::Colours::white);
    g.setFont (20.0f);
    g.drawFittedText ("LogicTail", getLocalBounds(), juce::Justification::centred, 1);
}

void LogicTailAudioProcessorEditor::resized() {}
