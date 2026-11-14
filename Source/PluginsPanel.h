#pragma once
#include "LiveConnection.h"

// The plugins a LiveRack server has scanned, and scanning for more, through
// the connection. (On this computer LiveRack shows its built-in plugin list,
// which also has the scan folder options.)
class PluginsPanel : public juce::Component,
                     private juce::ChangeListener,
                     private juce::Timer,
                     private juce::ListBoxModel
{
public:
    explicit PluginsPanel (LiveConnection&);
    ~PluginsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void reload();
    void filter();

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

    LiveConnection& live;
    juce::TextButton scanNew { "Scan for new plugins" }, rescanAll { "Rescan all" }, stop { "Stop" }, retryFailed { "Try failed ones again" };
    juce::TextEditor search;
    juce::ListBox list { "Plugins", this };

    std::vector<LivePlugin> all, shown;
    juce::StringArray failed;
    LiveScanStatus scan;
    bool wasScanning = false;

   #if LIVERACK_PREVIEW_HOOKS
    friend struct PreviewHooks;
   #endif
};
