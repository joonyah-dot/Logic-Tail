#pragma once
#include <JuceHeader.h>

namespace ParameterIDs {
    // REVERB parameters
    constexpr const char* reverb_gravity = "reverb_gravity";
    constexpr const char* reverb_size = "reverb_size";
    constexpr const char* reverb_predelay = "reverb_predelay";
    constexpr const char* reverb_feedback = "reverb_feedback";
    constexpr const char* reverb_mod_depth = "reverb_mod_depth";
    constexpr const char* reverb_mod_rate = "reverb_mod_rate";
    constexpr const char* reverb_lo = "reverb_lo";
    constexpr const char* reverb_hi = "reverb_hi";
    constexpr const char* reverb_resonance = "reverb_resonance";
    constexpr const char* reverb_freeze = "reverb_freeze";
    constexpr const char* reverb_kill_dry = "reverb_kill_dry";

    // DELAY parameters
    constexpr const char* delay_time = "delay_time";
    constexpr const char* delay_sync = "delay_sync";
    constexpr const char* delay_division = "delay_division";
    constexpr const char* delay_feedback = "delay_feedback";
    constexpr const char* delay_pingpong = "delay_pingpong";
    constexpr const char* delay_mod_rate = "delay_mod_rate";
    constexpr const char* delay_mod_depth = "delay_mod_depth";
    constexpr const char* delay_hp = "delay_hp";
    constexpr const char* delay_lp = "delay_lp";

    // GLOBAL parameters
    constexpr const char* routing_mode = "routing_mode";
    constexpr const char* parallel_balance = "parallel_balance";
    constexpr const char* global_mix = "global_mix";
    constexpr const char* input_gain = "input_gain";
    constexpr const char* output_gain = "output_gain";
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
