#pragma once
#include "RackEngine.h"

// Something a User Key, a MIDI control, a keyboard shortcut or an OSC
// command can do. The same list is used everywhere.
struct Action
{
    enum Type
    {
        none = 0,
        rackBypass,      // rack (or -1 = selected rack)
        pluginBypass,    // slotId (or slotId 0 = selected plugin)
        selectRack,      // rack
        openPlugin,      // slotId: show it in the plugin panel
        hotPlugin,       // number 1..12
        recallScene,     // number
        nextScene,
        prevScene,
        tapTempo,
        twoWindows,
        storeScene,
        showPage,        // number: 0 = All, 1.. = custom page
        fullView,        // the plugin panel fills the main window
        numTypes
    };

    Type type = none;
    int rack = -1;
    int slotId = 0;
    int number = 0;

    bool isToggle() const { return type == rackBypass || type == pluginBypass || type == twoWindows || type == fullView; }
    bool operator== (const Action& o) const { return type == o.type && rack == o.rack && slotId == o.slotId && number == o.number; }

    juce::String describe (const RackEngine&) const;    // "Bypass Lead Vox"
    juce::String category() const;                       // "rack bypass"

    void writeTo (juce::XmlElement&) const;
    static Action readFrom (const juce::XmlElement&);

    // A menu of every action for the current racks and plugins
    static void showChooser (const RackEngine&, const Action& current, int selectedRack,
                             std::function<void (Action)> onChosen);
};

// Implemented by the main window: runs actions and reports their state
struct ActionHost
{
    virtual ~ActionHost() = default;
    virtual void performAction (const Action&, int value) = 0; // value: -1 toggle/press, 0 off, 1 on
    virtual bool isActionActive (const Action&) const = 0;     // lit or not
    virtual int  getSelectedRack() const = 0;
    virtual int  getSelectedSlot() const = 0;
};
