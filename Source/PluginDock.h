#pragma once
#include "RackEngine.h"

// Shows the selected plugin's editor docked under the strips, with a tab for
// each plugin in the rack, like SuperRack's plugin pane.
class PluginDock : public juce::Component,
                   public juce::DragAndDropContainer,
                   public juce::DragAndDropTarget,
                   private juce::ComponentListener
{
public:
    explicit PluginDock (RackEngine&);
    ~PluginDock() override;

    void show (int rack, int slot);
    void releaseEditor(); // must be called before the shown plugin is destroyed or popped out
    juce::AudioProcessor* getShownProcessor() const { return shownProcessor; }


    std::function<void (int rack, int slot)> onSelectSlot, onPopOut, onRemove, onToggleBypass;
    std::function<void()> onPreferredHeightChanged;
    std::function<void (int fromRack, int fromSlot, int toRack, int toIndex, bool copy)> onSlotDropped;

    // Tabs can be dragged to reorder (or onto a rack strip)
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;
    void paintOverChildren (juce::Graphics&) override;

    // Height that shows the current plugin editor at full size
    int getPreferredHeight() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class TabButton;
    int dropIndexAt (juce::Point<int>) const;
    int dropIndex = -1;

    void rebuildTabs();
    void updateHeader();
    void layoutEditor();
    void componentMovedOrResized (juce::Component&, bool wasMoved, bool wasResized) override;

    RackEngine& engine;
    int rack = -1, slot = -1;

    juce::AudioProcessor* shownProcessor = nullptr;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::String message;

    juce::Label title;
    juce::TextButton bypassButton { "Bypass" }, popOutButton { "Pop Out" }, removeButton { "Remove" };
    juce::OwnedArray<juce::TextButton> tabs;
    juce::Component editorHolder; // plugin editors are often native views: no scrolling container
};
