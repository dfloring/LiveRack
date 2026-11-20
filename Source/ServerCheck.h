#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class RackEngine;
class WebRemote;
class LiveConnection;

// "Server check": the things that make a computer nobody sits at drop out,
// sleep, restart or lock during a service, each with a way to fix it.
namespace ServerCheck
{
    struct Item
    {
        enum State { good, lookAt, note };
        State state = good;
        juce::String title, detail;
        juce::String fixLabel;            // empty: nothing LiveRack can do for it
        std::function<void()> fix;        // message thread
    };

    // The computer's own settings: checked on a background thread, about once
    // a minute, and right away after a fix. Message thread.
    std::vector<Item> getSystemItems();
    void refreshSoon();
    std::function<void()>& onUpdated();   // called (message thread) when new results arrive

    // LiveRack's own settings (message thread)
    std::vector<Item> getAppItems (RackEngine&, WebRemote&);

    // The "Before the service" block at the top of the window: what LiveRack can
    // tell about the rig, in the order of docs/PRE-SERVICE.md. Built from the
    // same state on every screen. Message thread.
    struct BeforeRow
    {
        enum Light { good, lookAt, unknown };   // unknown: can't tell, never shown green
        Light light = unknown;
        juce::String title, detail;
    };
    std::vector<BeforeRow> getBeforeService (LiveConnection&);   // empty: not connected

    // A window listing them all, with the fix buttons (for this computer or a server)
    void showWindow (LiveConnection&);
}
