#pragma once
#include <juce_events/juce_events.h>

// Running LiveRack on a machine nobody sits at.
//
//  - Start at login: LiveRack starts when the computer has logged in (set the
//    computer itself to log in automatically).
//  - Restart after a crash: a small hidden "watcher" copy of LiveRack runs
//    alongside, on its own (not as LiveRack's child process, so ending
//    LiveRack with everything it started doesn't take the watcher too). If LiveRack disappears without quitting properly, the watcher
//    starts it again (it gives up after three crashes in five minutes), and
//    LiveRack comes back with the show it had.
namespace ServerMode
{
    constexpr const char* watcherUID = "--liverackwatcher";

    // Empty if start-at-login can be used, otherwise why not
    juce::String startAtLoginProblem();
    bool startsAtLogin();
    void setStartAtLogin (bool);

    bool restartsAfterCrash();
    void setRestartAfterCrash (bool);

    void appStarted();                 // the normal app, once it is up
    void appQuitting();                // the normal app, on a proper quit
    bool wasRestartedAfterCrash();     // true for a launch made by the watcher

    // Watcher process side: an object to keep alive, or nullptr for the normal app
    struct Watcher { virtual ~Watcher() = default; };
    std::unique_ptr<Watcher> startWatcherIfRequested (const juce::String& commandLine);
}
