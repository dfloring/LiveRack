#pragma once

#include <juce_core/juce_core.h>

// Network sockets that should stay with this process.
//
// On Windows, every process LiveRack starts (the plugin scanner, isolated
// plugins, the crash watcher) is given a copy of LiveRack's open sockets
// unless they are marked otherwise. A copy that outlives LiveRack keeps the
// port open with nothing answering on it, so the web remote, OSC remote or
// console link would stop responding after a restart.
namespace SocketUtil
{
    void keepInThisProcess (int rawSocketHandle);

    inline void keepInThisProcess (juce::StreamingSocket& s) { keepInThisProcess (s.getRawSocketHandle()); }
    inline void keepInThisProcess (juce::DatagramSocket& s)  { keepInThisProcess (s.getRawSocketHandle()); }

    // The address other devices on the network can reach this computer at,
    // or an empty string if there isn't one.
    juce::String getLanAddress();
}
