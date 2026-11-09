#pragma once
#include "RackEngine.h"

// What a snapshot recalls (each has its own), or what new ones start with: pills for global items, and a map
// of every rack and plugin. Click a plugin (or a rack name for the whole
// rack) to include or leave it alone. Lit = recalled.
class ScopeEditor : public juce::Component
{
public:
    ScopeEditor (RackEngine&, int snapshot);   // -1 = the scope new snapshots start with
    ~ScopeEditor() override;

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Map;
    RackEngine& engine;
    int target = -1;
    juce::ComboBox targetBox;
    juce::OwnedArray<juce::TextButton> pills;
    juce::Label summary, help;
    std::unique_ptr<Map> map;
    juce::Viewport viewport;
};
