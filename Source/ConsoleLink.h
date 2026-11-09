#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_osc/juce_osc.h>

// Listens to a mixing console on the network and reports scene recalls and
// channel selection, so LiveRack can follow the console (like Waves ProLink).
//
//  - Yamaha CL/QL: Yamaha Remote Control Protocol, TCP port 49280.
//    Scene recalls arrive as "NOTIFY sscurrent_ex MIXER:Lib/Scene <n>".
//  - Yamaha DM7: the same protocol, but the scene lists are "scene_a" and
//    "scene_b" (B is the second side in Split mode), and scene numbers have
//    two decimals ("4.00"). Point scenes like 4.10 recall snapshot 4.
//  - Behringer X32: OSC on UDP port 10023, kept alive with /xremote.
//    Scene: /-show/prepos/current, selected channel: /-stat/selidx.
//
// Everything received is kept in a log, so unknown messages (for example how
// a CL reports channel selection) can be discovered on a real console.
class ConsoleLink : private juce::Timer,
                    private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
{
public:
    enum class Type { off = 0, yamahaRcp, behringerX32, yamahaDm7 };

    struct CardSlot
    {
        juce::String slot;       // "MY Slot 1", "Card", "AES50 A"
        int card = 0;            // see cardTypeName
        int driver = 0;          // LiveRack audio driver it arrives on
        int firstChannel = 1;    // first driver channel (1-based)
        int numChannels = 64;
    };

    struct Settings
    {
        Type type = Type::off;
        juce::String address;
        juce::File sceneFolder;
        bool recallScenes = true;
        bool followSelection = true;

        // X32: which kind of recall to follow, and the OSC address that
        // announces it (learned from the console with "Learn")
        int x32Follow = 0; // 0 = scenes, 1 = snippets, 2 = cues
        juce::String x32Trigger[3] { "/-show/prepos/current", "/-show/prepos/current", "/-show/prepos/current" };

        int dm7Side = 0; // DM7: 0 = scene list A, 1 = scene list B (Split mode)

        juce::String name;            // label shown in LiveRack, e.g. "FOH CL5"
        std::vector<CardSlot> slots;  // what's in the console's card slots
    };

    // A console card slot and the audio driver channels it feeds (for labels)
    static constexpr int numCardTypes = 5;
    static juce::String cardTypeName (int);    // Empty, Dante, Waves SoundGrid, Stage box, Other
    static std::vector<CardSlot> defaultSlots (Type);

    static juce::String x32FollowName (int mode);

    // X32: listen for the next recall-type message and use its address
    void startLearning();
    bool isLearning() const { return learning; }

    ConsoleLink();
    ~ConsoleLink() override;

    void setSettings (const Settings&); // reconnects
    const Settings& getSettings() const { return settings; }

    bool isConnected() const    { return connected; }
    int  getCurrentScene() const { return currentScene; } // -1 = unknown
    juce::String getStatusText() const;
    juce::String getDisplayName() const;           // name, or the console type
    juce::uint32 getLastActivityMs() const { return lastActivityMs; }
    int getLastSelectedChannel() const { return lastSelectedChannel; }

    juce::StringArray getLog() const { return log; }
    int getLogCounter() const        { return logCounter; }

    // Called on the message thread
    std::function<void (int scene)> onSceneRecalled;
    std::function<void (int channel)> onChannelSelected; // 1-based input channel

    // Scene snapshot files: any "*.liverack" in the scene folder whose name
    // starts with the scene number, e.g. "12.liverack" or "012 Verse.liverack"
    juce::File fileForScene (int scene) const;
    juce::File newFileForScene (int scene) const;

    void saveSettingsTo (const juce::File&) const;
    void loadSettingsFrom (const juce::File&);

    static juce::String typeName (Type);
    static bool isYamaha (Type t) { return t == Type::yamahaRcp || t == Type::yamahaDm7; }
    juce::String rcpSceneList() const; // "MIXER:Lib/Scene", "scene_a" or "scene_b"

private:
    class RcpThread;

    void start();
    void stop();
    void timerCallback() override;
    void oscMessageReceived (const juce::OSCMessage&) override;
    void handleRcpLine (const juce::String&);
    void sendX32 (const juce::String& address);
    void sceneChanged (int scene);
    void addLog (const juce::String&);
    void post (const juce::String&); // from the network thread

    Settings settings;

    std::unique_ptr<RcpThread> rcp;
    std::unique_ptr<juce::DatagramSocket> oscSocket;
    juce::OSCReceiver oscReceiver;
    juce::OSCSender oscSender;

    juce::CriticalSection queueLock;
    juce::StringArray incoming;

    bool connected = false;
    bool x32Primed = false;
    bool learning = false;
    juce::uint32 learnStartedMs = 0;
    int currentScene = -1;
    int lastSelectedChannel = 0;
    juce::uint32 lastActivityMs = 0;
    void channelSelected (int channel);
    juce::uint32 lastSceneEventMs = 0, lastOscReceivedMs = 0;
    int ticks = 0;

    juce::StringArray log;
    int logCounter = 0;
};
