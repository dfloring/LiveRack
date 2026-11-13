#pragma once
#include "LiveConnection.h"
#include "Theme.h"

// Shows the selected plugin's editor docked under the strips, with a tab for
// each plugin in the rack, like SuperRack's plugin pane.
class PluginDock : public juce::Component,
                   public juce::DragAndDropContainer,
                   public juce::DragAndDropTarget,
                   private juce::ComponentListener
{
public:
    explicit PluginDock (LiveConnection&);   // the show, on this computer or a server
    ~PluginDock() override;

    void show (int rack, int slot);
    void releaseEditor(); // must be called before the shown plugin is destroyed or popped out
    juce::AudioProcessor* getShownProcessor() const { return shownProcessor; }


    std::function<void (int rack, int slot)> onSelectSlot, onPopOut, onRemove, onToggleBypass;
    std::function<void()> onPreferredHeightChanged;
    std::function<void (int delta)> onStepRack;    // previous / next rack

    // Rack view: the panel fills the main window instead of sharing it with the strips
    void setFullView (bool isFull, bool isAvailable);
    std::function<void (int fromRack, int fromSlot, int toRack, int toIndex, bool copy)> onSlotDropped;

    // Tabs can be dragged to reorder (or onto a rack strip)
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;
    void paintOverChildren (juce::Graphics&) override;

    // Rack meters in the header (0..1 on the meter scale), and a repaint for them
    std::function<float (int rack, bool output)> levelFor;
    void updateMeters();

    // Height that shows the current plugin editor at full size
    int getPreferredHeight() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class TabButton;
    int dropIndexAt (juce::Point<int>) const;
    int dropIndex = -1;
    int meterTicks = 0;

    void rebuildTabs();
    void updateHeader();
    void layoutEditor();
    void componentMovedOrResized (juce::Component&, bool wasMoved, bool wasResized) override;

    LiveConnection& engine;
    int rack = -1, slot = -1;

    juce::AudioProcessor* shownProcessor = nullptr;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::Point<int> naturalSize;   // the editor's own size, before fitting it in
    juce::Point<int> lastFitted;    // the size the panel last asked for
    bool fitting = false, poppedTooBig = false;
    juce::AudioProcessor* tooBig = nullptr;  // doesn't fit and can't resize: not shown here
    juce::String message;

    juce::Rectangle<int> headerArea, meterArea;
    Theme::IconButton prevRackButton { "Previous rack", "prev", "Previous rack" },
                      nextRackButton { "Next rack", "next", "Next rack" };
    juce::TextButton rackBypassButton { "Rack Bypass" }, bypassButton { "Bypass" },
                     popOutButton { "Pop Out" }, removeButton { "Remove" };
    bool fullView = false, fullViewAvailable = false;
    juce::OwnedArray<juce::TextButton> tabs;
    juce::Component editorHolder; // plugin editors are often native views: no scrolling container
};
