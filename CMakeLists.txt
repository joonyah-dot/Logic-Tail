cmake_minimum_required(VERSION 3.15)

project(LogicTail VERSION 0.1.0)

add_subdirectory(extern/JUCE)

juce_add_plugin(LogicTail
    COMPANY_NAME "KyleAudio"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    FORMATS VST3 Standalone
    PRODUCT_NAME "Logic-Tail"
)

# Generates JuceHeader.h + sets up include paths correctly
juce_generate_juce_header(LogicTail)

# Beginner-safe: avoids VST3/VST2 replacement automation warnings
target_compile_definitions(LogicTail PRIVATE
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_VST2_SDK=0
)

target_sources(LogicTail PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/PluginEntry.cpp
    Source/Utility/ParameterLayout.cpp
    Source/Utility/ParameterLayout.h
    Source/DSP/FilterUtils.cpp
    Source/DSP/FilterUtils.h
    Source/DSP/DelayEngine.cpp
    Source/DSP/DelayEngine.h
    Source/DSP/ReverbEngine.cpp
    Source/DSP/ReverbEngine.h
)

target_compile_features(LogicTail PRIVATE cxx_std_17)

target_link_libraries(LogicTail PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra

    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
