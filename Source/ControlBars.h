#pragma once
#include "Actions.h"

// Row of up to 12 pinned plugins: one click opens a plugin in the plugin
// panel. Right-click to unpin, drag to reorder.
class HotPluginsBar : public juce::Component
{
public:
    HotPluginsBar (RackEngine&, ActionHost&);

    std::function<void (int slotId)> onOpen;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr int preferredHeight = 64;

private:
    juce::Rectangle<int> tileBounds (int index) const;
    int tileAt (juce::Point<int>) const;

    RackEngine& engine;
    ActionHost& host;
    int dragFrom = -1, dragOver = -1;
};

// Assignable on-screen buttons (also F1-F8 on the keyboard). Each is lit
// when what it controls is on. Right-click to assign or MIDI Learn.
class UserKeysBar : public juce::Component
{
public:
    UserKeysBar (RackEngine&, ActionHost&);

    static constexpr int numKeys = 8;
    const Action& getKey (int i) const { return keys[(size_t) i]; }
    void setKey (int i, const Action&);
    void press (int i);

    juce::KeyPress getShortcut (int i) const { return shortcuts[(size_t) i]; }
    void setShortcut (int i, const juce::KeyPress&);
    int  keyForShortcut (const juce::KeyPress&) const;   // -1 = none

    std::function<void (int key)> onMidiLearn;   // learn a MIDI control for this key
    std::function<void (int key)> onSetShortcut; // pick keyboard keys for this key
    std::function<void()> onChanged;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    void save() const;
    void load();

    static constexpr int preferredHeight = 64;

private:
    juce::Rectangle<int> keyBounds (int index) const;

    RackEngine& engine;
    ActionHost& host;
    std::array<Action, numKeys> keys;
    std::array<juce::KeyPress, numKeys> shortcuts;
    int pressedKey = -1;
};

// Tabs above the strips: All, the custom pages, and "+ Page".
// Click to switch, right-click a page for its options.
class PageTabs : public juce::Component
{
public:
    explicit PageTabs (RackEngine&);

    std::function<void()> onAddPage;
    std::function<void (int page)> onPageMenu;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    static constexpr int preferredHeight = 38;

private:
    std::vector<juce::Rectangle<int>> layoutTabs() const; // All, pages..., +
    RackEngine& engine;
};
