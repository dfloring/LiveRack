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

    // This computer's network ports with an IPv4 address (loopback left out)
    struct Network
    {
        juce::String id;          // stays the same while plugged in: "en0", or the adapter's id on Windows
        juce::String name;        // "Ethernet 2", "Wi-Fi", "en0"
        juce::String address;     // 192.168.1.20
        juce::String broadcast;   // 192.168.1.255, empty if unknown
        bool linkLocal = false;   // 169.254.x.x: usually an audio network (Dante, SoundGrid) with no DHCP
        juce::uint32 ip = 0, mask = 0;
        juce::String describe() const { return name + "  " + address + (linkLocal ? "  (self-assigned)" : ""); }
    };
    std::vector<Network> getNetworks();

    // The network with this id now, if it's there
    bool findNetwork (const juce::String& id, Network& result);

    // A device at this address is on that network, or is this computer
    bool isOnNetworkOrLocal (const juce::String& ip, const Network&);
}
