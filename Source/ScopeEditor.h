#pragma once
#include "RackEngine.h"

// What the next stored snapshot recalls: pills for global items, and a map
// of every rack and plugin. Click a plugin (or a rack name for the whole
// rack) to include or leave it alone. Lit = recalled.
class ScopeEditor : public juce::Component
{
public:
    explicit ScopeEditor (RackEngine&);
    ~ScopeEditor() override;

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Map;
    RackEngine& engine;
    juce::OwnedArray<juce::TextButton> pills;
    juce::Label summary, help;
    std::unique_ptr<Map> map;
    juce::Viewport viewport;
};
