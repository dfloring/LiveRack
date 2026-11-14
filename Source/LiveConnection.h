#pragma once
#include "RackEngine.h"
#include "MidiControl.h"
#include "ConsoleLink.h"

class OscRemote;
class WebRemote;

// How LiveRack's screens reach the show.
//
// The screens (rack strips, the plugin panel, and more to come) read the show
// and send changes through a LiveConnection, never through the engine itself.
//   - LocalConnection: this computer's own engine. No network, no copy:
//     LiveRack Native works exactly as it did.
//   - RemoteConnection: a LiveRack server over the network, through the same
//     /live protocol the iPad uses (the coming LiveRack Client).
//
// Message thread only. Change messages say the show changed (remote: a new
// state arrived); meters are read on the screens' own timer.

struct LiveSlot
{
    bool exists = false;
    int id = 0;                 // stays with the plugin when it moves (snapshots, Hot Plugins)
    juce::String name;
    juce::String key;           // plugin family key (library settings such as gain reduction)
    bool bypassed = false;
    bool isolated = false;      // runs in its own process
};

// An audio driver, as the routing screen needs it
struct LiveDriver
{
    juce::String label;                 // short: the device's name, or "Driver 2"
    bool open = false;
    int numIns = 0, numOuts = 0;        // channels enabled
    juce::StringArray inNames, outNames;
    juce::StringArray blockLabels;      // console card slot for each block of 8 channels ("MY Slot 1"), or empty
};

// What an audio driver could be set to (its settings dialog)
struct LiveDriverOptions
{
    bool known = false;                 // false until a server has answered
    juce::String type, device;          // current
    struct Type { juce::String name; juce::StringArray devices; };
    std::vector<Type> types;
    juce::Array<double> rates;
    juce::Array<int> buffers;
    double rate = 0;
    int buffer = 0;
};

// A console link, as the Devices screen shows it
struct LiveConsole
{
    ConsoleLink::Settings settings;
    juce::String displayName, statusText;
    bool connected = false, learning = false;
    int currentScene = -1, lastSelectedChannel = 0;
    juce::String selectedRack;          // the rack on the last selected channel, if any
    juce::uint32 lastActivityMs = 0;    // on this computer's millisecond counter, 0 = never
    int logCounter = 0;                 // goes up as messages arrive (the log itself: getConsoleLog)
};

// The OSC remote or the web remote
struct LiveRemoteSettings
{
    bool enabled = false, listening = false;
    int port = 0, clients = 0;
    juce::String networkId, networkName, statusText, url;
    juce::uint32 lastActivityMs = 0;
};

// Server mode
struct LiveServerSettings
{
    bool startAtLogin = false, restartAfterCrash = false, multiCore = false;
    int cores = 1;
    juce::String loginProblem;          // why starting at login can't be turned on, if it can't
    juce::StringArray startApps;        // paths on the server
};

// One line of Server check
struct LiveCheckItem
{
    int state = 0;                      // ServerCheck::Item::State
    juce::String title, detail, fixLabel;
};

// A network the server is on
struct LiveNetwork
{
    juce::String id, name, label;
};

class LiveConnection : public juce::ChangeBroadcaster
{
public:
    ~LiveConnection() override = default;

    // Is the audio on this computer? (Plugin windows and raw meters are then local.)
    virtual bool isLocal() const = 0;
    virtual bool isConnected() const = 0;
    virtual juce::String getServerName() const = 0;

    //==========================================================================
    // The show
    virtual int getNumRacks() = 0;
    virtual RackConfig getRackConfig (int rack) = 0;
    virtual bool isRackBypassed (int rack) = 0;
    virtual int getNumSlots (int rack) = 0;
    virtual LiveSlot getSlot (int rack, int slot) = 0;
    virtual int getStereoPoint (int rack) = 0;             // slot index, -1 = none
    virtual double getRackTotalLatencyMs (int rack) = 0;
    virtual double getGroupLatencyMs (int group) = 0;
    virtual int getNumDrivers() = 0;
    virtual LiveDriver getDriver (int driver) = 0;

    // Audio drivers: how each is doing, and setting them up
    virtual DriverStatus getDriverStatus (int driver) = 0;
    virtual std::vector<RackEngine::HeavyPlugin> getHeaviestPlugins (int driver, int count) = 0;
    virtual bool addDriver() = 0;                      // false: no more can be added
    virtual void removeDriver (int driver) = 0;        // its racks move to driver 1
    virtual void refreshDriverOptions() = 0;           // a server is asked again; a change message follows
    virtual LiveDriverOptions getDriverOptions (int driver) = 0;
    // Empty type / device, rate 0, buffer 0 = leave as it is. Returns an error (local), or empty.
    virtual juce::String setDriver (int driver, const juce::String& type, const juce::String& device, double rate, int buffer) = 0;
    virtual std::vector<int> getVisibleRacks() = 0;        // the current page, in order

    // Processing time, as a share of the block: the longest in the last few seconds
    virtual float getRackLoad (int rack) = 0;
    virtual float getSlotLoad (int rack, int slot) = 0;

    //==========================================================================
    // Meters. Local: raw peaks since the last call. Remote: the server's meter
    // readings (0..1 on the meter scale) and gain reduction (dB, -1 = none shown,
    // 60 = muted).
    virtual float consumePeak (int rack, bool output, int channel) = 0;
    virtual float consumeSlotOutputPeak (int rack, int slot) = 0;
    virtual float getMeter (int rack, bool output) = 0;
    virtual float getGainReduction (int rack, int slot) = 0;
    virtual bool isGainReductionEstimated (int rack, int slot) = 0;   // worked out from levels ("~")

    // The plugin itself, when it runs on this computer (nullptr otherwise)
    virtual juce::AudioPluginInstance* getLocalPlugin (int rack, int slot) = 0;
    virtual juce::PluginDescription getSlotDescription (int rack, int slot) = 0;

    //==========================================================================
    // Changes
    virtual void setRackBypassed (int rack, bool) = 0;
    virtual void setRackRecallSafe (int rack, bool) = 0;
    virtual void setRackLatencyGroup (int rack, int group) = 0;
    virtual void setRackColour (int rack, int colourIndex) = 0;
    virtual void setRackName (int rack, const juce::String&) = 0;
    virtual void setRackRouting (int rack, int driver, const int inputs[2], const int outputs[2]) = 0;   // channels 0-based, -1 = unpatched
    virtual void setSlotBypassed (int rack, int slot, bool) = 0;

    //==========================================================================
    // Snapshots. Number -1 = the scope new snapshots start with.
    struct Snapshot
    {
        int number = 0;
        juce::String name;
        juce::Time stored;            // invalid for snapshots from before 0.17
        juce::String consoleScene;    // linked console scene ("4.10"), empty = by number
        juce::String midi;            // what triggers it ("Program 3 (ch 1)"), empty = nothing
    };

    virtual std::vector<Snapshot> getSnapshots() = 0;   // in number order
    bool hasSnapshot (int number)                 { for (auto& s : getSnapshots()) if (s.number == number) return true; return false; }
    juce::String getSnapshotName (int number)     { for (auto& s : getSnapshots()) if (s.number == number) return s.name; return {}; }
    juce::Time getSnapshotStoredTime (int number) { for (auto& s : getSnapshots()) if (s.number == number) return s.stored; return {}; }
    juce::String getSnapshotConsoleScene (int number) { for (auto& s : getSnapshots()) if (s.number == number) return s.consoleScene; return {}; }
    int nextFreeSnapshotNumber (int after)
    {
        const auto list = getSnapshots();
        for (int n = juce::jmax (1, after + 1);; ++n)
            if (std::none_of (list.begin(), list.end(), [n] (const Snapshot& s) { return s.number == n; }))
                return n;
    }

    // The details of one snapshot (a server sends them for the snapshot asked
    // for with watchSnapshot; until then these are empty or defaults)
    virtual void watchSnapshot (int number) = 0;
    virtual SnapshotScope getScopeFor (int number) = 0;
    virtual bool isSlotInScopeFor (int number, int rack, int slot) = 0;
    virtual bool snapshotHasSlot (int number, int rack, int slot) = 0;
    virtual std::vector<SnapshotDifference> differencesFromNow (int number) = 0;

    virtual void setScopeFor (int number, const SnapshotScope&) = 0;
    virtual void setSlotInScopeFor (int number, int rack, int slot, bool) = 0;
    virtual void renameSnapshot (int number, const juce::String&) = 0;
    virtual void deleteSnapshot (int number) = 0;
    virtual bool duplicateSnapshot (int from, int to) = 0;   // false if 'to' is taken
    virtual bool renumberSnapshot (int from, int to) = 0;
    virtual void setSnapshotConsoleScene (int number, const juce::String& scene) = 0;

    //==========================================================================
    // Consoles
    virtual int getNumConsoles() = 0;
    virtual LiveConsole getConsole (int console) = 0;
    virtual juce::StringArray getConsoleLog (int console) = 0;    // a server sends it while watched
    virtual void watchConsoleLog (int console) = 0;               // -1 = none
    virtual void setConsoleSettings (int console, const ConsoleLink::Settings&) = 0;
    virtual void addConsole() = 0;
    virtual void removeConsole (int console) = 0;
    virtual void learnConsoleTrigger (int console) = 0;           // X32: use the next recall's address

    // Remotes and server settings
    virtual LiveRemoteSettings getOscSettings() = 0;
    virtual LiveRemoteSettings getWebSettings() = 0;
    virtual void setOscSettings (bool enabled, int port, const juce::String& networkId, const juce::String& networkName) = 0;
    virtual void setWebSettings (bool enabled, int port, const juce::String& networkId, const juce::String& networkName) = 0;
    virtual std::vector<LiveNetwork> getNetworks() = 0;
    virtual LiveServerSettings getServerSettings() = 0;
    virtual void setServerSettings (bool startAtLogin, bool restartAfterCrash, bool multiCore, const juce::StringArray& startApps) = 0;
    virtual std::vector<LiveCheckItem> getServerCheck() = 0;
    virtual void fixServerCheck (const juce::String& title) = 0;   // fixes open on the server's screen
    virtual void recheckServer() = 0;

    // MIDI program changes and snapshots (on the computer with the audio)
    virtual MidiControl::SnapshotLink getSnapshotLink() = 0;
    virtual void setSnapshotLink (const MidiControl::SnapshotLink&) = 0;
    virtual juce::StringArray getMidiInputNames() = 0;
    virtual juce::StringArray getMidiOutputNames() = 0;
};

//==============================================================================
// This computer's engine
// (Native's screens are refreshed by the main window when the engine changes,
// as before, so this sends no change messages of its own.)
class LocalConnection : public LiveConnection
{
public:
    explicit LocalConnection (RackEngine& e) : engine (e) {}

    // Console card slot for a driver's channel (1-based), from the main window
    std::function<juce::String (int driver, int channel)> slotLabelFor;

    // The main window's consoles and remotes
    struct Services
    {
        std::vector<std::unique_ptr<ConsoleLink>>* consoles = nullptr;
        OscRemote* osc = nullptr;
        WebRemote* web = nullptr;
        std::function<void()> addConsole, consolesChanged;
        std::function<void (int)> removeConsole;
        std::function<juce::String (int console, int channel)> rackForChannel;
    };
    void setServices (Services s) { services = std::move (s); }

    int getNumConsoles() override;
    LiveConsole getConsole (int) override;
    juce::StringArray getConsoleLog (int) override;
    void watchConsoleLog (int) override {}
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

    static int numBlocks (const DriverStatus& st) { return (juce::jmax (32, st.numIns, st.numOuts) + 7) / 8; }

    LiveDriver getDriver (int d) override
    {
        LiveDriver v;
        const auto st = engine.getDriverStatus (d);
        v.label = engine.getDriverLabel (d);
        v.open = st.open;
        v.numIns = st.numIns;
        v.numOuts = st.numOuts;
        if (auto* dev = engine.getDeviceManager (d).getCurrentAudioDevice())
        {
            v.inNames = dev->getInputChannelNames();
            v.outNames = dev->getOutputChannelNames();
        }
        for (int b = 0; b < numBlocks (st); ++b)
            v.blockLabels.add (slotLabelFor != nullptr ? slotLabelFor (d, b * 8 + 1) : juce::String());
        return v;
    }

    DriverStatus getDriverStatus (int d) override   { return engine.getDriverStatus (d); }
    std::vector<RackEngine::HeavyPlugin> getHeaviestPlugins (int d, int n) override { return engine.getHeaviestPlugins (d, n); }
    bool addDriver() override                       { return engine.addDriver() >= 0; }
    void removeDriver (int d) override              { engine.removeDriver (d); }
    void refreshDriverOptions() override            {}

    LiveDriverOptions getDriverOptions (int d) override
    {
        LiveDriverOptions o;
        if (! juce::isPositiveAndBelow (d, engine.getNumDrivers()))
            return o;

        auto& dm = engine.getDeviceManager (d);
        o.known = true;
        o.type = dm.getCurrentAudioDeviceType();
        for (auto* type : dm.getAvailableDeviceTypes())
            o.types.push_back ({ type->getTypeName(), type->getDeviceNames (false) });

        if (auto* dev = dm.getCurrentAudioDevice())
        {
            o.device = dev->getName();
            o.rates = dev->getAvailableSampleRates();
            o.buffers = dev->getAvailableBufferSizes();
            o.rate = dev->getCurrentSampleRate();
            o.buffer = dev->getCurrentBufferSizeSamples();
        }
        return o;
    }

    juce::String setDriver (int d, const juce::String& type, const juce::String& device, double rate, int buffer) override
    {
        if (! juce::isPositiveAndBelow (d, engine.getNumDrivers()))
            return "No such driver";

        auto& dm = engine.getDeviceManager (d);
        if (type.isNotEmpty() && type != dm.getCurrentAudioDeviceType())
            dm.setCurrentAudioDeviceType (type, true);

        auto setup = dm.getAudioDeviceSetup();
        if (device.isNotEmpty())
        {
            setup.outputDeviceName = setup.inputDeviceName = device;
            setup.useDefaultInputChannels = setup.useDefaultOutputChannels = true;
        }
        if (rate > 0)   setup.sampleRate = rate;
        if (buffer > 0) setup.bufferSize = buffer;

        const auto error = dm.setAudioDeviceSetup (setup, true);
        engine.saveSettings();
        return error;
    }

    void setRackRouting (int r, int driver, const int inputs[2], const int outputs[2]) override
    {
        auto cfg = engine.getRackConfig (r);
        cfg.driver = driver;
        for (int c = 0; c < 2; ++c) { cfg.input[c] = inputs[c]; cfg.output[c] = outputs[c]; }
        engine.setRackConfig (r, cfg);
    }
    void setMidi (MidiControl* m) { midi = m; }

    RackEngine& getEngine() { return engine; }

    bool isLocal() const override     { return true; }
    bool isConnected() const override { return true; }
    juce::String getServerName() const override { return "This computer"; }

    int getNumRacks() override                       { return engine.getNumRacks(); }
    RackConfig getRackConfig (int r) override        { return engine.getRackConfig (r); }
    bool isRackBypassed (int r) override             { return engine.isRackBypassed (r); }
    int getNumSlots (int r) override                 { return engine.getNumSlots (r); }
    int getStereoPoint (int r) override              { return engine.getStereoPoint (r); }
    double getRackTotalLatencyMs (int r) override    { return engine.getRackTotalLatencyMs (r); }
    double getGroupLatencyMs (int g) override        { return engine.getGroupLatencyMs (g); }
    int getNumDrivers() override                     { return engine.getNumDrivers(); }
    std::vector<int> getVisibleRacks() override      { return engine.getVisibleRacks(); }
    float getRackLoad (int r) override               { return engine.getRackLoad (r).peak; }
    float getSlotLoad (int r, int s) override        { return engine.getSlotLoad (r, s).peak; }

    LiveSlot getSlot (int r, int s) override
    {
        LiveSlot slot;
        if (auto* p = engine.getPlugin (r, s))
        {
            slot.exists = true;
            slot.id = engine.getSlotId (r, s);
            slot.name = p->getName();
            slot.bypassed = engine.isSlotBypassed (r, s);
            slot.isolated = engine.isSlotIsolated (r, s);
        }
        return slot;
    }

    float consumePeak (int r, bool out, int ch) override     { return engine.consumePeak (r, out, ch); }
    float consumeSlotOutputPeak (int r, int s) override      { return engine.consumeSlotOutputPeak (r, s); }
    float getMeter (int, bool) override                      { return 0.0f; }
    float getGainReduction (int, int) override               { return -1.0f; }
    bool isGainReductionEstimated (int, int) override        { return false; }

    juce::AudioPluginInstance* getLocalPlugin (int r, int s) override   { return engine.getPlugin (r, s); }
    juce::PluginDescription getSlotDescription (int r, int s) override { return engine.getSlotDescription (r, s); }

    void setRackBypassed (int r, bool b) override              { engine.setRackBypassed (r, b); }
    void setRackRecallSafe (int r, bool b) override            { engine.setRackRecallSafe (r, b); }
    void setRackLatencyGroup (int r, int g) override           { engine.setRackLatencyGroup (r, g); }
    void setRackColour (int r, int c) override                 { engine.setRackColour (r, c); }
    void setRackName (int r, const juce::String& n) override   { engine.setRackName (r, n); }
    void setSlotBypassed (int r, int s, bool b) override       { engine.setSlotBypassed (r, s, b); }

    std::vector<Snapshot> getSnapshots() override
    {
        std::vector<Snapshot> list;
        for (auto& sn : engine.getSnapshots())
            list.push_back ({ sn.number, sn.name, engine.getSnapshotStoredTime (sn.number), engine.getSnapshotConsoleScene (sn.number),
                              midi != nullptr ? midi->describeTriggerFor (sn.number) : juce::String() });
        return list;
    }

    void watchSnapshot (int) override {}
    SnapshotScope getScopeFor (int n) override                          { return engine.getScopeFor (n); }
    bool isSlotInScopeFor (int n, int r, int s) override                { return engine.isSlotInScopeFor (n, r, s); }
    bool snapshotHasSlot (int n, int r, int s) override                 { return engine.snapshotHasSlot (n, r, s); }
    std::vector<SnapshotDifference> differencesFromNow (int n) override { return engine.differencesFromNow (n); }

    void setScopeFor (int n, const SnapshotScope& sc) override          { engine.setScopeFor (n, sc); }
    void setSlotInScopeFor (int n, int r, int s, bool in) override      { engine.setSlotInScopeFor (n, r, s, in); }
    void renameSnapshot (int n, const juce::String& name) override      { engine.renameSnapshot (n, name); }
    void deleteSnapshot (int n) override                                { engine.deleteSnapshot (n); }
    bool duplicateSnapshot (int from, int to) override                  { return engine.duplicateSnapshot (from, to); }
    bool renumberSnapshot (int from, int to) override                   { return engine.renumberSnapshot (from, to); }
    void setSnapshotConsoleScene (int n, const juce::String& sc) override { engine.setSnapshotConsoleScene (n, sc); }

    MidiControl::SnapshotLink getSnapshotLink() override         { return midi != nullptr ? midi->getSnapshotLink() : MidiControl::SnapshotLink(); }
    void setSnapshotLink (const MidiControl::SnapshotLink& l) override { if (midi != nullptr) midi->setSnapshotLink (l); }
    juce::StringArray getMidiInputNames() override               { return midi != nullptr ? midi->getInputNames() : juce::StringArray(); }
    juce::StringArray getMidiOutputNames() override              { return MidiControl::getOutputNames(); }

private:
    ConsoleLink* console (int i) const;

    RackEngine& engine;
    MidiControl* midi = nullptr;
    Services services;
};
