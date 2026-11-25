#pragma once
#include "LiveConnection.h"

// The Console links tab: console channels down the side, racks across the top.
// Click a cell to link that rack to that console channel (so selecting the
// channel on the console selects the rack); click a lit cell to unlink.
// One channel links to one rack; a stereo rack takes two (first click L,
// second R). Works on any LiveConnection, so Native and Client are the same.
class ConsoleLinksGrid : public juce::Component,
                         private juce::Timer
{
public:
    explicit ConsoleLinksGrid (LiveConnection&);
    ~ConsoleLinksGrid() override;

    void refresh();

    // What a click on a cell does (also used by the tests)
    void toggleLink (int rack, int channel);

    // Where a rack's name is drawn, and the column it belongs to (for the tests)
    static juce::Rectangle<float> headerLabelOnScreen (int column);
    static juce::Rectangle<int> headerColumn (int column);

    void paint (juce::Graphics&) override;
    void resized() override;

#if LIVERACK_PREVIEW_HOOKS
    friend struct PreviewHooks;
   #endif
private:
    class Grid;
    void timerCallback() override;
    int numRows() const;
    juce::String rowLabel (int channel) const;
    bool linked (int rack, int channel) const;

    LiveConnection& engine;
    int console = 0;
    ConsoleLink::Layout layout;   // the channels the shown console has
    bool allRacks = false;
    int highlighted = 0;     // the channel last selected on the console
    juce::String message;    // shown instead of the grid, if any

    juce::OwnedArray<juce::TextButton> consoleTabs;
    juce::TextButton allRacksButton { "All racks" };
    juce::Label help;
    std::unique_ptr<Grid> grid;
    juce::Viewport viewport;
};
