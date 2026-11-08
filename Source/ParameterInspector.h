#pragma once
#include "PluginLibrary.h"
#include "GainReduction.h"

// Lists every parameter a plugin reports, with live values, and shows where
// LiveRack gets its gain reduction from. Pick a parameter and press
// "Use for GR" to teach LiveRack a plugin's GR meter.
class ParameterInspector : public juce::Component,
                           private juce::ListBoxModel,
                           private juce::Timer
{
public:
    ParameterInspector (juce::AudioPluginInstance&, const juce::PluginDescription& loadedFrom,
                        PluginLibrary&, std::function<void()> onGainReductionChanged);
    ~ParameterInspector() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::AudioProcessor& getPlugin() { return plugin; }

    // MIDI Learn for the selected parameter (followSelected: whichever rack is selected)
    std::function<void (int paramIndex, bool followSelected)> onMidiLearn;

private:
    int getNumRows() override { return visible.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void timerCallback() override;
    void refilter();
    void updateSource();

    juce::AudioPluginInstance& plugin;
    PluginLibrary& library;
    std::function<void()> onChanged;
    juce::String familyKey;

    juce::Label sourceLabel, hint;
    juce::TextEditor filter;
    juce::TextButton useButton { "Use for GR" }, clearButton { "Clear" }, learnButton { "MIDI Learn" };
    juce::ListBox list { "Parameters", this };
    juce::Array<int> visible;
    GainReduction::Probe probe;
};
