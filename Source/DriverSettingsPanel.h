#pragma once
#include "LiveConnection.h"

// An audio driver's settings, through the connection: device type, device,
// sample rate and buffer size. For a driver on a LiveRack server (on this
// computer LiveRack shows the fuller built-in dialog, with channel choices).
class DriverSettingsPanel : public juce::Component,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    DriverSettingsPanel (LiveConnection&, int driver);
    ~DriverSettingsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void fill();
    void apply (bool typeChanged, bool deviceChanged);

    LiveConnection& live;
    const int driver;
    juce::ComboBox type, device, rate, buffer;
    juce::String status;
    bool waiting = false;

   #if LIVERACK_PREVIEW_HOOKS
    friend struct PreviewHooks;
   #endif
};
