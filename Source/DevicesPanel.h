#pragma once
#include "LiveConnection.h"
#include "ConsoleLink.h"
#include "MidiControl.h"
#include "OscRemote.h"
#include "WebRemote.h"

// Audio drivers, consoles (with their card slots) and controllers, each as
// a panel with live status lights
class DevicesPanel : public juce::Component,
                     private juce::Timer
{
public:
    struct Host
    {
        RackEngine& engine;
        LiveConnection& live;     // the show, here or on a server (the driver cards so far)
        std::vector<std::unique_ptr<ConsoleLink>>& consoles;
        MidiControl& midi;
        OscRemote& osc;
        WebRemote& web;
        std::function<void (int driver)> openDriverSettings;
        std::function<void (int console)> openConsoleSettings;
        std::function<void()> addConsole;
        std::function<void (int console)> removeConsole;
        std::function<void()> consolesChanged;  // save settings
        std::function<juce::String (int console, int channel)> rackForChannel;
        std::function<void()> openShortcuts;
        std::function<int()> shortcutCount;
    };

    explicit DevicesPanel (Host);
    ~DevicesPanel() override;

    void refresh();   // rebuild the panels (drivers or consoles added/removed)
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class DriverCard;
    class ConsoleCard;
    class ControllerCard;
    class Content;

    void timerCallback() override;

    Host host;
    std::unique_ptr<Content> content;
    juce::Viewport viewport;
};
