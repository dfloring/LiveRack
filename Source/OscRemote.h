#pragma once
#include "SocketUtil.h"
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_osc/juce_osc.h>
#include <map>

// Remote control over OSC (for Bitfocus Companion, TouchOSC, scripts...).
// Listens on a UDP port; anyone who sends a message becomes a client and
// gets state feedback sent back to the address and port it sent from.
//
// Commands (see README for the full list):
//   /liverack/rack/<n>/bypass [0|1]     /liverack/rack/<n>/select
//   /liverack/rack/<n>/slot/<m>/bypass [0|1]
//   /liverack/key/<n>/press             /liverack/hot/<n>/open
//   /liverack/scene/recall <n>          /liverack/scene/next   /liverack/scene/prev
//   /liverack/tap                       /liverack/bpm <bpm>
//   /liverack/subscribe                 /liverack/ping   (keep feedback coming)
class OscRemote : private juce::Thread,
                  private juce::Timer
{
public:
    OscRemote();
    ~OscRemote() override;

    void setEnabled (bool, int port);
    // Answer only devices on this network (and this computer); empty = all
    void setNetwork (const juce::String& networkId, const juce::String& networkName);
    juce::String getNetworkId() const   { return networkId; }
    juce::String getNetworkName() const { return networkName; }
    bool isEnabled() const   { return enabled; }
    bool isListening() const { return socket != nullptr; }
    int  getPort() const     { return port; }
    int  getNumClients() const { return (int) clients.size(); }
    juce::uint32 getLastActivityMs() const { return lastActivityMs; }
    juce::String getStatusText() const;

    // Message thread: a command arrived
    std::function<void (const juce::String& address, const juce::Array<juce::var>& args)> onMessage;

    // Feedback: sent to every client, only when the value changes
    void send (const juce::String& address, const juce::var& value);

    void saveSettings() const;
    void loadSettings();

private:
    struct Incoming { juce::String ip; int port; juce::String address; juce::Array<juce::var> args; };
    struct Client   { juce::String ip; int port; juce::uint32 lastSeen; };

    void run() override;
    void timerCallback() override;
    void parsePacket (const char* data, int size, const juce::String& ip, int fromPort, int depth);
    void sendTo (const Client&, const juce::String& address, const juce::var& value);

    bool enabled = false;
    int port = 9010;
    juce::String networkId, networkName;
    SocketUtil::Network chosenNetwork;
    bool networkFound = false;
    std::unique_ptr<juce::DatagramSocket> socket;
    juce::OSCSender sender;

    juce::CriticalSection queueLock;
    std::vector<Incoming> queue;

    std::vector<Client> clients;
    std::map<juce::String, juce::var> lastSent;
    juce::uint32 lastActivityMs = 0;
};
