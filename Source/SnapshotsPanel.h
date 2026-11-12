#pragma once
#include "RackEngine.h"
#include "MidiControl.h"
#include <set>

// The Snapshots screen: every snapshot in the show, and for the one picked,
// what it recalls (its scope), what recalling it would change now, and how
// it's triggered. Replaces the Scope window.
class SnapshotsPanel : public juce::Component,
                       private juce::Timer
{
public:
    struct Host
    {
        RackEngine& engine;
        MidiControl& midi;
        std::function<int()> currentSnapshot;
        std::function<void (int)> recall;
        std::function<void (int, const juce::String&, const SnapshotStoreFilter&)> store;
        std::function<void (const juce::String& title, const juce::String& message,
                            const juce::String& okText, std::function<void()> onOk)> confirm;
        std::function<juce::String (int)> consoleSceneFor;   // "FOH CL5 scene 3", or empty
        std::function<void (const juce::String&)> note;      // a line for the status bar
        std::function<juce::String()> lastConsoleScene;      // as the console last reported it ("4.10")
    };

    explicit SnapshotsPanel (Host);
    ~SnapshotsPanel() override;

    void refresh();                 // the show changed
    void select (int number);       // -1: the scope new snapshots start with

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class List;
    class Scope;
    class Diffs;

    void timerCallback() override;
    void updateDifferences();
    void updateHeader();
    void updateMidiControls();
    void applyMidiControls();
    void showStoreOverMenu();
    void storeTicked();
    void startPicking();
    void finishPicking (bool store);
    void storeNew();
    void renumber();
    bool hasSelection() const;

    Host host;
    RackEngine& engine;
    int selected = -1;

    // The list
    juce::TextEditor newNumber, newName;
    juce::TextButton storeNewButton { "Store" }, defaultsButton { "Scope for new snapshots" };
    std::unique_ptr<List> list;
    juce::Viewport listView;

    // Program changes
    juce::ToggleButton followPC { "Program changes recall snapshots" }, sendPC { "Send a program change on recall" };
    juce::ComboBox pcInput, pcChannel, outDevice, outChannel;
    juce::TextEditor firstSnapshot;

    // The snapshot picked
    juce::TextEditor nameEdit;
    juce::TextButton recallButton { "Recall" }, storeOverButton { "Store over" }, duplicateButton { "Duplicate" },
                     renumberButton { "Renumber" }, deleteButton { "Delete" };
    juce::String infoLine;
    juce::TextButton consoleSceneButton { "Console scene" };
    void linkConsoleScene();

    // What it recalls
    juce::OwnedArray<juce::TextButton> pills;
    std::unique_ptr<Scope> scope;
    juce::Viewport scopeView;
    bool picking = false;
    std::set<int> pickedSlots, pickedRacks;     // by id
    juce::TextButton pickStore { "Store" }, pickCancel { "Cancel" };

    // Differs from now
    std::vector<SnapshotDifference> differences;
    std::set<juce::String> unticked;            // "rack:slot"
    std::unique_ptr<Diffs> diffs;
    juce::Viewport diffView;
    juce::TextButton storeTickedButton;
    std::map<int, bool> changedSince;           // snapshot -> plugins differ now

    juce::Rectangle<int> leftPanel, headerPanel, scopePanel, diffPanel, midiPanel, pickBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnapshotsPanel)
};
