#include "ParameterLayout.h"

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // REVERB GROUP
    auto reverbGroup = std::make_unique<juce::AudioProcessorParameterGroup>("reverb", "Reverb", "|");

    reverbGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ParameterIDs::reverb_gravity, 1},
        "Gravity",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 0.01f),
        50.0f,
        juce::AudioParameterFloatAttributes().withLabel("")
    ));

    reverbGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ParameterIDs::reverb_size, 1},
        "Size",
        juce::NormalisableRange<float>(0.0f, 120.0f, 0.01f),
        60.0f
    ));

    reverbGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ParameterIDs::reverb_predelay, 1},
        "Pre-Delay",
        juce::NormalisableRange<float>(0.0f, 2000.0f, 0.1f),
        40.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")
    ));

    reverbGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ParameterIDs::reverb_feedback, 1},
        "Feedback",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")
    ));

    reverbGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ParameterIDs::reverb_mod_depth, 1},
        "Mod Depth",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        40.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")
    ));

    reverbGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ParameterIDs::reverb_mod_rate, 1},
        "Mod Rate",
        juce::NormalisableRange<float>(0.1f, 5.0f, 0.01f, 0.35f),  // Skewed for lower range
        0.8f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")
    ));

    reverbGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ParameterIDs::reverb_lo, 1},
        "Lo EQ",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")
    ));

    reverbGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ParameterIDs::reverb_hi, 1},
        "Hi EQ",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")
    ));

    reverbGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ParameterIDs::reverb_resonance, 1},
        "Resonance",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")
    ));

    reverbGroup->addChild(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ParameterIDs::reverb_freeze, 1},
        "Freeze",
        false
    ));

    reverbGroup->addChild(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ParameterIDs::reverb_kill_dry, 1},
        "Kill Dry",
        false
    ));

    // DELAY GROUP
    auto delayGroup = std::make_unique<juce::AudioProcessorParameterGroup>("delay", "Delay", "|");

    delayGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ParameterIDs::delay_time, 1},
        "Time",
        juce::NormalisableRange<float>(1.0f, 2000.0f, 0.1f, 0.25f),  // Skewed for 50-500ms sweet spot
        500.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")
    ));

    delayGroup->addChild(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ParameterIDs::delay_sync, 1},
        "Tempo Sync",
        false
    ));

    delayGroup->addChild(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ParameterIDs::delay_division, 1},
        "Division",
        juce::StringArray{
            "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D",
            "1/4T", "1/4", "1/4D", "1/2T", "1/2", "1/2D", "1/1"
        },
        8  // Default to "1/4"
    ));

    delayGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ParameterIDs::delay_feedback, 1},
        "Feedback",
        juce::NormalisableRange<float>(0.0f, 95.0f, 0.1f),
        35.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")
    ));

    delayGroup->addChild(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ParameterIDs::delay_pingpong, 1},
        "Ping Pong",
        false
    ));

    delayGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ParameterIDs::delay_mod_rate, 1},
        "Mod Rate",
        juce::NormalisableRange<float>(0.1f, 10.0f, 0.01f, 0.30f),
        0.5f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")
    ));

    delayGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ParameterIDs::delay_mod_depth, 1},
        "Mod Depth",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        15.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")
    ));

    delayGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ParameterIDs::delay_hp, 1},
        "HP Filter",
        juce::NormalisableRange<float>(20.0f, 2000.0f, 1.0f, 0.25f),  // Logarithmic
        80.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")
    ));

    delayGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ParameterIDs::delay_lp, 1},
        "LP Filter",
        juce::NormalisableRange<float>(200.0f, 20000.0f, 1.0f, 0.25f),  // Logarithmic
        8000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")
    ));

    // GLOBAL GROUP
    auto globalGroup = std::make_unique<juce::AudioProcessorParameterGroup>("global", "Global", "|");

    globalGroup->addChild(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ParameterIDs::routing_mode, 1},
        "Routing",
        juce::StringArray{"Series D>R", "Series R>D", "Parallel"},
        2  // Default to "Parallel"
    ));

    globalGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ParameterIDs::parallel_balance, 1},
        "Balance",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")
    ));

    globalGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ParameterIDs::global_mix, 1},
        "Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")
    ));

    globalGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ParameterIDs::input_gain, 1},
        "Input",
        juce::NormalisableRange<float>(-24.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")
    ));

    globalGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ParameterIDs::output_gain, 1},
        "Output",
        juce::NormalisableRange<float>(-24.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")
    ));

    layout.add(std::move(reverbGroup));
    layout.add(std::move(delayGroup));
    layout.add(std::move(globalGroup));

    return layout;
}
