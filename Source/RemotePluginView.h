#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>

// A plugin window on a LiveRack server, shown in LiveRack Client: a live
// picture of it (the same /view stream the iPad uses), with this computer's
// mouse, scroll wheel and keyboard played back into the real window.
class RemotePluginView : public juce::Component,
                         private juce::Thread,
                         private juce::Timer
{
public:
    // what: "view=<rack>:<slot>" for a plugin, "app=<n>" for a start app
    RemotePluginView (const juce::String& host, int port, const juce::String& what);
    ~RemotePluginView() override;

    // The window's own size in points, {0, 0} until the first picture arrives
    juce::Point<int> getNaturalSize() const { return naturalSize; }
    std::function<void()> onNaturalSizeChanged;

    // Top-centred (the plugin panel) or centred (a window of its own)
    void setCentred (bool c) { centred = c; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void run() override;
    bool connectAndRun();
    void timerCallback() override;
    void received (const juce::String& text);
    void showMenu (const juce::var& items);
    void send (const juce::var& event);
    juce::var pointerEvent (const juce::String& type, const juce::MouseEvent&) const;
    juce::Rectangle<float> pictureArea() const;
    static juce::String modsOf (juce::ModifierKeys);

    const juce::String host, what;
    const int port;
    float captureScale = 1.0f;

    // Message thread
    juce::Image picture;
    juce::Point<int> naturalSize;
    juce::String message { "Opening the plugin window..." };
    bool centred = false, ended = false;
    juce::var pendingMove;           // the latest move, sent at most 60 times a second
    juce::Point<int> lastMouse;
    juce::String buttonDown { "l" };

    juce::CriticalSection queueLock;
    juce::StringArray outgoing;
    std::atomic<bool> stopped { false };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

   #if LIVERACK_PREVIEW_HOOKS
    friend struct ClientPreview;
   #endif
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RemotePluginView)
};

// A server's plugin window in a window of its own (Pop Out in LiveRack Client)
class RemotePluginWindow : public juce::DocumentWindow
{
public:
    RemotePluginWindow (const juce::String& title, const juce::String& host, int port, const juce::String& what,
                        std::function<void()> onClose);

    void closeButtonPressed() override;
    const juce::String what;

private:
    std::function<void()> onClose;
    bool sized = false;
};
