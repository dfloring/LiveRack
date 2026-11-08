#pragma once
#include "PluginLibrary.h"

// The "+ Add plugin" browser: type to search, filter by category, favourites
// and recents on the left. Each plugin appears once; the right version for
// the rack (mono or stereo) is loaded automatically.
class PluginPicker : public juce::Component,
                     private juce::KeyListener
{
public:
    // wantFor: which version (mono / stereo / mono-to-stereo) suits the rack for a plugin
    using WantFn = std::function<PluginLibrary::Want (const PluginLibrary::Family&)>;
    PluginPicker (PluginLibrary&, WantFn wantFor, const juce::String& rackDescription,
                  std::function<void (const juce::PluginDescription&)> onChosen);
    ~PluginPicker() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    std::function<void()> onClose; // asked to close (after choosing, or Escape)

private:
    struct Entry
    {
        const PluginLibrary::Family* family = nullptr;
        int version = -1; // -1 = pick automatically for the rack
    };

    class ResultsModel;
    class SideModel;

    void refilter();
    void choose (const Entry&);
    void showContextMenu (const Entry&);
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;

    PluginLibrary& library;
    WantFn wantFor;
    juce::String rackDescription;
    std::function<void (const juce::PluginDescription&)> onChosen;

    int categoryFilter = -1; // -1 = all
    std::vector<Entry> results;

    juce::TextEditor search;
    juce::OwnedArray<juce::TextButton> categoryButtons;
    juce::ToggleButton allVersions { "Show every version" };
    juce::Label countLabel, dragHint;

    std::unique_ptr<ResultsModel> resultsModel;
    std::unique_ptr<SideModel> sideModel;
    juce::ListBox resultsList, sideList;
};
