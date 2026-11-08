#pragma once
#include "RackEngine.h"
class PluginLibrary;

// Racks shown side by side as channel strips, SuperRack-style: a coloured
// header, the plugin slots, in/out meters, and BYPASS / SAFE buttons.
// Scrolls sideways when there are more racks than fit.
class RackStripsView : public juce::Component,
                       public juce::DragAndDropContainer
{
public:
    explicit RackStripsView (RackEngine&);
    ~RackStripsView() override;

    std::function<void (int rack, int slot)> onSelect;    // slot -1 = the rack itself
    std::function<void (int rack)>           onAddPlugin;
    std::function<void (int rack, int slot)> onSlotMenu;  // right-click on a slot
    std::function<void (int rack)>           onRackMenu;  // right-click on the rack
    std::function<void (int rack)>           onEditRack;  // double-click on the header
    std::function<void()>                    onAddRack;
    std::function<void (int rack)>           onOpenRouting;         // click on the I/O line
    std::function<void (int rack)>           onEditConsoleChannel;  // click on "CH"
    std::function<void (int rack, const RackConfig&)> onFormatChange; // mono <> stereo
    // A plugin dragged in from the plugin picker (identifier string)
    std::function<void (const juce::String& pluginId, int toRack, int toIndex)> onPluginDropped;
    // A plugin was dragged to (toRack, insertion index); copy = Option held
    std::function<void (int fromRack, int fromSlot, int toRack, int toIndex, bool copy)> onSlotDropped;

    // Wrap racks into rows when the view is tall enough (each row keeps
    // room for about six plugins)
    void setStackRows (bool);
    bool getStackRows() const;

    void setLibrary (PluginLibrary*);   // for GR settings and categories
    void forgetMeters();                 // re-check plugins' GR sources

    void setSelection (int rack, int slot);
    void refresh();
    void updateMeters();
    void scrollToRack (int rack);
    float getMaxGainReduction (int rack) const;
    float getOutputLevel (int rack) const;      // 0..1 on the meter's scale
   #if LIVERACK_PREVIEW_HOOKS
    void previewMenu (int which, int rack);
   #endif // dB shown on the rack's plugins (60 = muted)
    juce::Rectangle<int> getSlotScreenBounds (int rack, int slot) const;
    juce::Rectangle<int> getAddRowScreenBounds (int rack) const;

    void resized() override;

    static constexpr int numColours = 8;
    static juce::Colour colourForIndex (int index);
    static juce::String colourName (int index);
    static juce::Colour colourFor (const RackEngine&, int rack);

#if LIVERACK_PREVIEW_HOOKS
    friend struct PreviewHooks;
   #endif
private:
    class Content;
    juce::Viewport viewport;
    std::unique_ptr<Content> content;
};
