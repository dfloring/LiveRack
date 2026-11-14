#pragma once
#include "LiveConnection.h"
#include <map>

// A LiveRack server over the network, through its /live protocol (the same
// one the iPad remote uses). Keeps a copy of the server's state, updated as
// the server sends it, and sends changes as commands.
//
// The network runs on its own thread; the state and meters are swapped in on
// the message thread, so the screens read them like the local engine.
class RemoteConnection : public LiveConnection,
                         private juce::Thread
{
public:
    RemoteConnection (const juce::String& host, int port);
    ~RemoteConnection() override;

    bool isLocal() const override     { return false; }
    bool isConnected() const override { return connected; }
    juce::String getServerName() const override;

    int getNumRacks() override;
    RackConfig getRackConfig (int rack) override;
    bool isRackBypassed (int rack) override;
    int getNumSlots (int rack) override;
    LiveSlot getSlot (int rack, int slot) override;
    int getStereoPoint (int rack) override;
    double getRackTotalLatencyMs (int rack) override;
    double getGroupLatencyMs (int group) override;
    int getNumDrivers() override;
    LiveDriver getDriver (int driver) override;
    void setRackRouting (int rack, int driver, const int inputs[2], const int outputs[2]) override;
    DriverStatus getDriverStatus (int driver) override;
    std::vector<RackEngine::HeavyPlugin> getHeaviestPlugins (int driver, int count) override;
    bool addDriver() override;
    void removeDriver (int driver) override;
    void refreshDriverOptions() override;
    LiveDriverOptions getDriverOptions (int driver) override;
    juce::String setDriver (int driver, const juce::String& type, const juce::String& device, double rate, int buffer) override;

    int getNumConsoles() override;
    LiveConsole getConsole (int) override;
    juce::StringArray getConsoleLog (int) override;
    void watchConsoleLog (int) override;
    void setConsoleSettings (int, const ConsoleLink::Settings&) override;
    void addConsole() override;
    void removeConsole (int) override;
    void learnConsoleTrigger (int) override;
    LiveRemoteSettings getOscSettings() override;
    LiveRemoteSettings getWebSettings() override;
    void setOscSettings (bool, int, const juce::String&, const juce::String&) override;
    void setWebSettings (bool, int, const juce::String&, const juce::String&) override;
    std::vector<LiveNetwork> getNetworks() override;
    LiveServerSettings getServerSettings() override;
    void setServerSettings (bool, bool, bool, const juce::StringArray&) override;
    std::vector<LiveCheckItem> getServerCheck() override;
    void fixServerCheck (const juce::String&) override;
    void recheckServer() override;

    std::vector<RackPage> getPages() override;
    void setPages (std::vector<RackPage>) override;
    int getCurrentPage() override;
    void setCurrentPage (int) override;
    std::vector<int> getHotPlugins() override;
    void setHotPlugins (std::vector<int>) override;
    void refreshKnownPlugins() override;
    std::vector<LivePlugin> getKnownPlugins() override;
    juce::StringArray getFailedPlugins() override;
    LiveScanStatus getScanStatus() override;
    void startScan (bool) override;
    void cancelScan() override;
    void forgetPlugin (const juce::String&) override;
    void clearFailedPlugins() override;
    std::vector<int> getVisibleRacks() override;
    float getRackLoad (int rack) override;
    float getSlotLoad (int rack, int slot) override;

    float consumePeak (int, bool, int) override       { return 0.0f; }
    float consumeSlotOutputPeak (int, int) override   { return 0.0f; }
    float getMeter (int rack, bool output) override;
    float getGainReduction (int rack, int slot) override;
    bool isGainReductionEstimated (int rack, int slot) override;

    juce::AudioPluginInstance* getLocalPlugin (int, int) override { return nullptr; }
    juce::PluginDescription getSlotDescription (int rack, int slot) override;

    void setRackBypassed (int rack, bool) override;
    void setRackRecallSafe (int rack, bool) override;
    void setRackLatencyGroup (int rack, int group) override;
    void setRackColour (int rack, int colourIndex) override;
    void setRackName (int rack, const juce::String&) override;
    void setSlotBypassed (int rack, int slot, bool) override;

    std::vector<Snapshot> getSnapshots() override;
    void watchSnapshot (int number) override;
    SnapshotScope getScopeFor (int number) override;
    bool isSlotInScopeFor (int number, int rack, int slot) override;
    bool snapshotHasSlot (int number, int rack, int slot) override;
    std::vector<SnapshotDifference> differencesFromNow (int number) override;
    void setScopeFor (int number, const SnapshotScope&) override;
    void setSlotInScopeFor (int number, int rack, int slot, bool) override;
    void renameSnapshot (int number, const juce::String&) override;
    void deleteSnapshot (int number) override;
    bool duplicateSnapshot (int from, int to) override;
    bool renumberSnapshot (int from, int to) override;
    void setSnapshotConsoleScene (int number, const juce::String&) override;

    MidiControl::SnapshotLink getSnapshotLink() override;
    void setSnapshotLink (const MidiControl::SnapshotLink&) override;
    juce::StringArray getMidiInputNames() override;
    juce::StringArray getMidiOutputNames() override;

    // Any command of the protocol ({"cmd": ...}), for what has no method yet
    void sendCommand (const juce::var& command);

    // The latest state, as the server sent it
    const juce::var& getState() const { return state; }

    // Asks the server for something ("shows", "descriptions"...); the reply
    // arrives on the message thread. Nothing comes back if the link drops.
    void request (const juce::String& what, std::function<void (const juce::var& data)> onReply);

    juce::String getHost() const    { return host; }
    int getPort() const             { return port; }
    juce::String getServerVersion() const { return serverVersion; }   // empty until it says hello
    bool hasEverConnected() const   { return everConnected; }

private:
    void run() override;
    bool connectAndRun();
    void received (const juce::String& message);
    bool sendQueued (juce::StreamingSocket&);
    void sendQuery();
    juce::var watchedSnapshot (int number) const;   // its details, if they're for that number

    juce::var rackState (int rack) const;
    juce::var slotState (int rack, int slot) const;
    juce::var rackInfo (int rack) const;
    juce::var rackMeters (int rack) const;

    const juce::String host;
    const int port;

    // Message thread
    juce::var state, meters, deviceOptions, networks, pluginList;
    juce::String serverName, serverVersion;
    bool everConnected = false;
    std::map<int, std::function<void (const juce::var&)>> pendingReplies;
    int nextRequestId = 100;
    std::atomic<bool> connected { false };

    juce::CriticalSection queueLock;
    juce::StringArray outgoing;
    std::atomic<int> watched { std::numeric_limits<int>::min() };   // the snapshot whose details we asked for
    std::atomic<int> watchedLog { -1 };                              // the console whose log we asked for
    juce::uint32 activityFromAge (const juce::var& ageMs) const;
    LiveRemoteSettings remoteFrom (const juce::var&, const juce::var& ageMs) const;

    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RemoteConnection)
};
