#pragma once
#include "RackEngine.h"
#include "MidiControl.h"

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
    RackEngine& engine;
    MidiControl* midi = nullptr;
};
