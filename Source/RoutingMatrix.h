#pragma once
#include "LiveConnection.h"

// Console-style crosspoint routing: rack inputs (or outputs) down the side,
// audio driver channels across the top. Click a crosspoint to patch it,
// click a lit one to unpatch. Stereo racks patch L and R separately.
class RoutingMatrix : public juce::Component
{
public:
    explicit RoutingMatrix (LiveConnection&);   // the show, here or on a server
    ~RoutingMatrix() override;

    std::function<void (int rack)> onSelectRack;

    void setSelectedRack (int);
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

#if LIVERACK_PREVIEW_HOOKS
    friend struct PreviewHooks;
   #endif
private:
    class Grid;
    void updateTabs();
    bool outputFree (int rack, int driver, int channel) const;
    void autoPatchOutputs (int rack, RackConfig&, int side, int channel);
    juce::String note; // e.g. why an output wasn't auto-patched

    LiveConnection& engine;
    bool outputs = false;
    int driver = 0, page = 0, selectedRack = -1, selectedSide = 0;

    juce::TextButton inputsTab { "Rack inputs" }, outputsTab { "Rack outputs" };
    juce::OwnedArray<juce::TextButton> driverTabs, pageTabs;
    juce::Label help;
    std::unique_ptr<Grid> grid;
    juce::Viewport viewport;
};
