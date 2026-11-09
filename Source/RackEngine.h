#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

// One insert rack: mono or stereo, fed from channels of one audio driver and
// returned to channels of the same driver. Channel numbers are 0-based
// indexes into that driver's ENABLED channels; -1 = not patched.
struct RackConfig
{
    juce::String name { "Rack" };
    int id = 0;                  // stays the same across sessions (pages refer to it)
    bool stereo = false;         // stereo in and out
    bool monoToStereo = false;   // mono in, stereo out (when stereo is false)
    int driver = 0;              // which audio driver this rack runs on
    int input[2]  { 0, 1 };      // L (or mono), R
    int output[2] { 0, 1 };
    int consoleChannel = 0;      // console input channel this rack belongs to (1-based, 0 = none)
    int console = 0;             // which linked console that channel is on
    bool recallSafe = false;     // scene recalls leave this rack alone
    int colourIndex = -1;        // palette entry, -1 = automatic (by position)
    int latencyGroup = 0;        // 0 = none, 1..8 = A..H

    int numInputs() const   { return stereo ? 2 : 1; }
    int numOutputs() const  { return (stereo || monoToStereo) ? 2 : 1; }
    int numChannels() const { return numOutputs(); }
    bool isMonoToStereo() const { return monoToStereo && ! stereo; }
    juce::String formatName() const { return stereo ? "ST" : monoToStereo ? "M>S" : "MONO"; }
    int effectiveColour (int rackIndex) const { return colourIndex >= 0 ? colourIndex : rackIndex; }
    void patchContiguous (int firstIn, int firstOut)
    {
        input[0] = firstIn;  input[1] = firstIn + 1;
        output[0] = firstOut; output[1] = firstOut + 1;
    }
    static juce::String groupName (int g) { return g <= 0 ? juce::String ("-") : juce::String::charToString ((juce::juce_wchar) ('A' + g - 1)); }
};

// A custom page: a named set of racks, in the user's order (by rack id)
struct RackPage
{
    juce::String name;
    std::vector<int> rackIds;
};

// What recalling a snapshot changes (besides which plugins are in scope, kept
// per plugin). Snapshots never add, remove or move plugins or racks.
struct SnapshotScope
{
    bool pluginSettings = true, pluginBypass = true, rackBypass = true, bpm = true;
};

// A snapshot stored in the show: settings for the plugins already in it,
// matched by rack and plugin id
struct ShowSnapshot
{
    int number = 0;
    juce::String name;
    std::shared_ptr<juce::XmlElement> data;
};

// Live figures for one audio driver, for the status bar and device panel
struct DriverStatus
{
    bool open = false;
    juce::String typeName, deviceName;
    double sampleRate = 0;
    int bufferSize = 0, numIns = 0, numOuts = 0, ioLatency = 0;
    double cpu = 0, peakCpu = 0;
    int dropouts = 0;            // since launch (-1 = the driver can't report them)
    juce::Time lastDropout;
    int numRacks = 0;
};

// The audio engine: any number of audio drivers, each running its own racks
// on its own audio thread. Each rack has its own processing graph, so a
// high-latency plugin only delays its own rack (or its latency group).
class RackEngine : public juce::ChangeBroadcaster,
                   private juce::Timer
{
public:
    RackEngine();
    ~RackEngine() override;

    //==============================================================================
    // Audio drivers (message thread only). Driver 0 always exists.
    static constexpr int maxDrivers = 4;
    int  getNumDrivers() const { return (int) drivers.size(); }
    int  addDriver();                 // returns its index, or -1
    void removeDriver (int);          // its racks move to driver 0
    juce::AudioDeviceManager& getDeviceManager (int driver = 0);
    DriverStatus getDriverStatus (int driver) const;
    juce::String getDriverLabel (int driver) const; // short: device name or "Driver 2"
    void updateDriverStats();         // call regularly from the UI (tracks peaks and dropouts)

    juce::AudioPluginFormatManager& getFormatManager()  { return formatManager; }
    juce::KnownPluginList&          getKnownPlugins()   { return knownPlugins; }
    juce::File getDeadMansPedalFile() const;
    double getSampleRate (int driver = 0) const;

    //==============================================================================
    // Racks (message thread only)
    int  addRack (const RackConfig&);
    void removeRack (int rack);
    void setRackConfig (int rack, const RackConfig&);
    RackConfig getRackConfig (int rack) const;
    int  getNumRacks() const { return (int) racks.size(); }
    void setRackBypassed (int rack, bool);
    bool isRackBypassed (int rack) const;
    void setRackRecallSafe (int rack, bool); // no audio reconfiguration
    void setRackColour (int rack, int colourIndex);
    void setRackName (int rack, const juce::String&);
    void setRackLatencyGroup (int rack, int group);
    int  getRackId (int rack) const;
    int  findRackById (int id) const;    // -1 if gone
    // Peak level since the last call (0..1+), for meters. channel 0/1.
    float consumePeak (int rack, bool output, int channel);
    int  getRackLatencySamples (int rack) const;     // the plugins' own latency
    double getRackTotalLatencyMs (int rack) const;   // including latency-group padding
    double getGroupLatencyMs (int group) const;      // what the group is aligned to
    void clearAll();

    //==============================================================================
    // Plugins inside a rack (message thread only)
    bool addPlugin (int rack, const juce::PluginDescription&, juce::String& error, int slotId = 0);
    void removePlugin (int rack, int slot);
    void moveSlot (int rack, int from, int to);             // swap neighbours
    void moveSlotTo (int rack, int from, int toIndex);      // drag-and-drop reorder
    bool copyPlugin (int fromRack, int fromSlot, int toRack, int toIndex, juce::String& error); // with settings
    void setSlotBypassed (int rack, int slot, bool);
    bool isSlotBypassed (int rack, int slot) const;
    int  getNumSlots (int rack) const;
    juce::AudioPluginInstance* getPlugin (int rack, int slot) const;
    juce::PluginDescription getSlotDescription (int rack, int slot) const;
    float consumeSlotOutputPeak (int rack, int slot);

    // Mono-to-stereo racks: where the chain turns stereo, and each slot's width
    int  getStereoPoint (int rack) const;      // slot index, -1 = none (still mono)
    void setStereoPoint (int rack, int slot);  // for plugins that can change layout
    int  getSlotOutputChannels (int rack, int slot) const;
    // Swap a plugin for another version of itself (e.g. Mono -> Stereo), keeping its settings
    bool replacePlugin (int rack, int slot, const juce::PluginDescription&, juce::String& error);

    // Each loaded plugin has an id that stays the same while it is moved
    // around (used by Hot Plugins, User Keys and controller mappings)
    int  getSlotId (int rack, int slot) const;
    bool findSlot (int slotId, int& rack, int& slot) const;

    //==============================================================================
    // Snapshot scope: which plugins a snapshot recalls, and what else
    bool isSlotInScope (int rack, int slot) const;
    void setSlotInScope (int rack, int slot, bool);
    const SnapshotScope& getScope() const { return scope; }   // what new snapshots start with
    void setScope (const SnapshotScope&);

    // Each snapshot keeps its own scope (copied from the one above when it's
    // stored). snapshot -1 = the scope new snapshots start with.
    SnapshotScope getScopeFor (int snapshot) const;
    void setScopeFor (int snapshot, const SnapshotScope&);
    bool isSlotInScopeFor (int snapshot, int rack, int slot) const;
    void setSlotInScopeFor (int snapshot, int rack, int slot, bool);
    bool snapshotHasSlot (int snapshot, int rack, int slot) const;

    // Hot Plugins: up to 12 pinned plugins (by slot id)
    static constexpr int maxHotPlugins = 12;
    const std::vector<int>& getHotPlugins() const { return hotPlugins; }
    void setHotPlugins (std::vector<int>);
    bool isHot (int slotId) const;
    void pinHot (int slotId);
    void unpinHot (int slotId);

    // Custom pages (saved with the session, not in snapshots)
    const std::vector<RackPage>& getPages() const { return pages; }
    void setPages (std::vector<RackPage>);
    int  getCurrentPage() const { return currentPage; }   // -1 = All
    void setCurrentPage (int);
    std::vector<int> getVisibleRacks() const;             // rack indices on the current page, in order
    bool isRackOnPage (int rack, int page) const;
    void setRackOnPage (int rack, int page, bool);

    // Tempo for tempo-synced plugins (delays, modulation)
    double getBpm() const { return bpm.load(); }
    void setBpm (double);
    void tapTempo();

    //==============================================================================
    // The show: racks, plugins and their state, routing, pages, Hot Plugins,
    // BPM, scope, the stored snapshots, and extra sections the UI keeps
    // (user keys, window layout). One .liverack file holds all of it.
    std::unique_ptr<juce::XmlElement> createShow() const;
    bool loadShow (const juce::XmlElement&, juce::String& error);
    void newShow();

    // The show's current racks and plugins without pages or snapshots
    // (also how pre-0.13 files and scene files are laid out)
    std::unique_ptr<juce::XmlElement> createSnapshot() const;
    bool snapshotMatchesCurrentLayout (const juce::XmlElement&, bool checkIds = true) const;

    juce::File getShowFile() const { return showFile; }
    void setShowFile (const juce::File& f) { showFile = f; }
    bool isShowEdited() const { return showEdited; }
    void setShowEdited (bool e) { showEdited = e; }

    // Named sections of the show kept for the UI (copied in and out whole)
    const juce::XmlElement* getShowExtra (const juce::String& tag) const;
    void setShowExtra (const juce::XmlElement&);

    //==============================================================================
    // Snapshots stored in the show, by number (console scene numbers map to
    // these). Recall only changes plugin settings and bypass, rack bypass and
    // BPM, following the scope; racks marked recall safe are left alone.
    const std::vector<ShowSnapshot>& getSnapshots() const { return snapshots; }
    const ShowSnapshot* getSnapshotInfo (int number) const;
    void storeSnapshot (int number, const juce::String& name = {});
    bool recallSnapshot (int number, juce::String& note);   // false = no such snapshot
    void renameSnapshot (int number, const juce::String& name);
    void deleteSnapshot (int number);
    // A pre-0.13 scene file (a full rig) becomes a snapshot, if it has the
    // same racks and plugins as the show
    bool importSceneFile (const juce::XmlElement&, int number, const juce::String& name, juce::String& error);

    static juce::File getSettingsDir();

    // The open show is kept on quit (and every so often) and restored on launch
    void saveSession();
    bool restoreSession (juce::String& error);
    void saveSettings();

private:
    struct Rack;
    struct Driver;
    Rack* getRack (int) const;
    Driver* getDriverFor (const Rack&) const;

    void timerCallback() override;      // keeps latency groups aligned
    void rebuildDriverLists();          // after racks are added/removed/moved between drivers
    void configureRack (Rack&, bool updateDriverLists);
    void applyShowInPlace (const juce::XmlElement&);
    bool loadRig (const juce::XmlElement&, juce::String& error);
    std::unique_ptr<juce::XmlElement> captureSnapshotData() const;
    void relayoutRack (Rack&);          // re-request plugin channel layouts along the chain
    void detachRack (Rack&);            // take off its driver before rebuilding its graph
    void readScope (const juce::XmlElement&);
    void readHot (const juce::XmlElement&);
    int  attachSlotId (juce::AudioProcessorGraph::Node&, int preferredId = 0);
    void loadDriverSettings();

    std::vector<std::unique_ptr<Driver>> drivers;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

    // Owned rack list: changes on the message thread only. Each driver keeps
    // its own list of the racks it runs, swapped under that driver's lock.
    std::vector<std::unique_ptr<Rack>> racks;

    SnapshotScope scope;
    std::vector<ShowSnapshot> snapshots;
    juce::OwnedArray<juce::XmlElement> showExtras;
    juce::File showFile;
    bool showEdited = false;
    std::vector<RackPage> pages;
    int currentPage = -1;
    std::vector<int> hotPlugins;
    std::atomic<double> bpm { 120.0 };
    juce::Array<double> tapTimes;

   #if LIVERACK_PREVIEW_HOOKS
    friend struct PreviewHooks;
    void previewPrepare (double sr, int bs);
    void previewProcess (juce::AudioBuffer<float>& in, juce::AudioBuffer<float>& out);
   #endif
};
