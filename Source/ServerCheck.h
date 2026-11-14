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

    // A window listing them all, with the fix buttons (for this computer or a server)
    void showWindow (LiveConnection&);
}
