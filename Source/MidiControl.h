#pragma once
#include "Actions.h"
#include <map>

// One MIDI control and what it does
struct MidiMapping
{
    juce::String device;      // input name, empty = any device
    int kind = 1;             // 0 = note, 1 = CC, 2 = program change
    int channel = 1;          // 1..16
    int number = 0;

    bool isParameter = false;
    Action action;            // when it isn't a parameter

    // Parameter target. followSelected: the plugin at slotPosition in
    // whichever rack is selected (one set of knobs for every rack)
    bool followSelected = true;
    int slotId = 0;
    int slotPosition = 0;
    int paramIndex = 0;
    juce::String paramName;

    juce::String describeControl() const;
    juce::String describeTarget (const RackEngine&) const;
    bool sameControl (const MidiMapping& o) const
    {
        return device == o.device && kind == o.kind && channel == o.channel && number == o.number;
    }
};

// MIDI controllers: any number of inputs at once, MIDI Learn for actions
// and plugin parameters, and LED feedback to the matching MIDI output.
class MidiControl : private juce::MidiInputCallback,
                    private juce::Timer
{
public:
    MidiControl (RackEngine&, ActionHost&);
    ~MidiControl() override;

    // Only devices the user has added are opened (and listed in Devices)
    struct DeviceInfo { juce::String name; bool connected = false; juce::uint32 lastActivity = 0; int mappings = 0; };
    std::vector<DeviceInfo> getDevices() const;
    juce::StringArray getDevicesToAdd() const;   // plugged in, not added yet
    void addDevice (const juce::String& name);
    void removeDevice (const juce::String& name, bool keepMappings);
    void rescan();

    std::vector<MidiMapping>& getMappings() { return mappings; }
    void removeMapping (int index);
    void clearMappings();

    // MIDI Learn: the next control moved is mapped to this
    void learnAction (const Action&);
    void learnParameter (int rack, int slot, int paramIndex, bool followSelected);
    void cancelLearn();
    bool isLearning() const { return learning; }
    juce::String getLearnText() const;

    std::function<void()> onChanged;   // learned, devices changed

    // Snapshots and program changes: program changes coming in recall
    // snapshots, and recalling a snapshot sends one out
    struct SnapshotLink
    {
        bool follow = false;
        juce::String input;            // an added device, empty = any
        int channel = 0;               // 0 = any
        int firstSnapshot = 1;         // what program 1 (the first) recalls
        bool send = false;
        juce::String output;           // a MIDI output by name
        int outChannel = 1;
    };
    const SnapshotLink& getSnapshotLink() const { return link; }
    void setSnapshotLink (const SnapshotLink&);
    static juce::StringArray getOutputNames();
    juce::StringArray getInputNames() const;   // the devices added
    void snapshotRecalled (int number);        // sends the program change if set to
    juce::String describeTriggerFor (int snapshot) const; // "Program 3 (ch 1)" etc., empty if none

    void save() const;
    void load();

private:
   #if LIVERACK_PREVIEW_HOOKS
    friend struct PreviewHooks;
   #endif
    struct Device
    {
        juce::String name;
        juce::MidiDeviceInfo info;
        std::unique_ptr<juce::MidiInput> input;
        std::unique_ptr<juce::MidiOutput> output;
        juce::uint32 lastActivity = 0;
    };

    struct Incoming { juce::String device; juce::MidiMessage message; };

    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void timerCallback() override;
    void handle (const Incoming&);
    void apply (const MidiMapping&, const juce::MidiMessage&);
    void sendFeedback();
    void openDevices();
    void checkForChanges();     // plugged in / unplugged
    juce::StringArray lastSeenInputs;

    RackEngine& engine;
    ActionHost& host;

    std::vector<std::unique_ptr<Device>> devices;
    juce::StringArray addedDevices;
    std::vector<MidiMapping> mappings;
    std::map<int, bool> lastFeedback; // mapping index -> lit

    SnapshotLink link;
    std::unique_ptr<juce::MidiOutput> snapshotOut;
    juce::String snapshotOutName;
    bool recallFromMidi = false;      // don't echo a program change straight back
    void openSnapshotOutput();

    bool learning = false;
    MidiMapping learnTemplate;
    juce::uint32 learnStartedMs = 0;

    juce::CriticalSection queueLock;
    std::vector<Incoming> queue;
    int ticks = 0;
};
