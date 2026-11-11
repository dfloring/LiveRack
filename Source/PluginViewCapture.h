#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <memory>

// A live picture of one plugin window for the web remote.
//
// It captures the window as JPEG frames and plays the remote's taps, drags,
// scrolls and typing back into the window as real mouse and key events, so
// the plugin's own interface works from an iPad.
//
// Mac: ScreenCaptureKit (needs Screen Recording permission) and AppKit events.
// Windows: copies the window's part of the screen, real mouse and key input.
// Linux previews: JUCE drawing only (JUCE-drawn editors).
class PluginViewCapture : public std::enable_shared_from_this<PluginViewCapture>
{
public:
    struct Options
    {
        float scale   = 2.0f;   // capture pixels per window point
        float quality = 0.7f;   // JPEG quality 0..1
    };

    // Message thread. Returns nullptr and sets error if this computer can't.
    static std::shared_ptr<PluginViewCapture> create (juce::DocumentWindow& window, Options, juce::String& error);
    static bool isSupported();

    virtual ~PluginViewCapture() = default;

    //==========================================================================
    // Message thread

    // One remote event as JSON: {"t":"down"|"move"|"up", "x":0..1, "y":0..1,
    // "b":"l"|"r", "n":clicks, "mods":"sacm"}, {"t":"wheel", x, y, dx, dy},
    // {"t":"key", "k":"Enter"|"Backspace"|...}, {"t":"text", "s":"..."},
    // {"t":"menu", "path":[i, j]}, {"t":"menuCancel"}, {"t":"opts", scale, q}
    virtual void input (const juce::var& event) = 0;

    // Stops capturing. Called when the remote's view ends.
    virtual void stop() = 0;

    // The plugin window is going away (closed on the computer, plugin removed).
    void windowClosed (const juce::String& why);

    //==========================================================================
    // Any thread (the web remote's connection thread)

    // Encodes the newest frame if one arrived since the last call.
    virtual bool takeFrame (juce::MemoryBlock& jpeg) = 0;

    // Messages for the remote (JSON text): errors, menus, closed.
    bool takeMessage (juce::String& json);

    bool hasEnded() const { return ended.load(); }

protected:
    void postMessage (const juce::var& object);
    void postError (const juce::String& message);   // and ends the view
    static int modifierFlagsFrom (const juce::String& mods); // bit 0 shift, 1 alt, 2 ctrl, 3 cmd

    std::atomic<bool> ended { false };

private:
    juce::CriticalSection messageLock;
    juce::StringArray messages;
};
