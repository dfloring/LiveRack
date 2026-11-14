#pragma once
#include <juce_core/juce_core.h>
#include <functional>

// LiveRack runs as Native (the show and its audio on this computer) or as a
// Client (the screens, for a LiveRack server elsewhere on the network). The
// application sets switchTo; the main window's menu calls it.
namespace AppMode
{
    // Message thread. Swaps the main window over (after the caller returns).
    inline std::function<void (bool client)> switchTo;

    inline bool isClient = false;
}
