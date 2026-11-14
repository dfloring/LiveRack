#pragma once
#include <juce_core/juce_core.h>

// Listens for LiveRack servers announcing themselves on the network (the
// web remote's UDP beacon, every two seconds), for LiveRack Client's
// "Connect to a server" screen.
class ServerFinder : private juce::Thread
{
public:
    ServerFinder();
    ~ServerFinder() override;

    struct Server
    {
        juce::String name, host, version;
        int port = 0;
        juce::uint32 lastSeenMs = 0;
    };

    // The servers heard from in the last few seconds, by name. Any thread.
    std::vector<Server> getServers() const;

    bool isListening() const { return listening.load(); }

private:
    void run() override;

    mutable juce::CriticalSection lock;
    std::vector<Server> servers;
    std::atomic<bool> listening { false };
};
