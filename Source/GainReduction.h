#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <limits>

// How LiveRack gets gain reduction from a plugin. VST3/AU have no single
// standard, so in order of preference:
//  1. A parameter the user picked in "Show Parameters" for this plugin
//  2. PreSonus' VST3 gain-reduction extension (FabFilter, Softube, ...)
//  3. A read-only meter parameter (category or name like "Gain Reduction")
// Plugins with none of these can show an estimate from the level in and out.
namespace GainReduction
{
    enum class Source { none, userParameter, presonus, parameter };

    struct Probe
    {
        Source source = Source::none;
        int parameterIndex = -1;
        juce::String describe (juce::AudioProcessor&) const;
    };

    constexpr float muted = std::numeric_limits<float>::infinity();

    Probe probe (juce::AudioPluginInstance&, int userParameterIndex);
    int findMeterParameter (juce::AudioProcessor&); // -1 if none

    // Positive dB of reduction (3.0 = 3 dB), or `muted`. False if unavailable.
    bool read (juce::AudioPluginInstance&, const Probe&, float& db);

    float parameterToDb (juce::AudioProcessorParameter&);
    bool readPresonus (juce::AudioPluginInstance&, float& db);
}
