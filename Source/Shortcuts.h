#pragma once
#include "Actions.h"

// Keyboard shortcuts: one per User Key (F1-F8 by default), plus any number
// of extra shortcuts for other actions (T = tap tempo by default)
struct Shortcut
{
    juce::KeyPress key;
    Action action;
};

namespace Shortcuts
{
    juce::String describe (const juce::KeyPress&);    // "Cmd+3", "F1", "none"
    juce::String toText (const juce::KeyPress&);      // for saving
    juce::KeyPress fromText (const juce::String&);
    bool isReserved (const juce::KeyPress&);          // taken by macOS / the app menu

    std::vector<Shortcut> loadExtras();
    void saveExtras (const std::vector<Shortcut>&);
}

// "Press the keys you want": captures one key combination
class KeyCapture : public juce::Component
{
public:
    explicit KeyCapture (std::function<void (juce::KeyPress)> onCaptured);

    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;

    // Opens a small window and calls back with the keys (not called if cancelled)
    static void launch (const juce::String& forWhat, std::function<void (juce::KeyPress)> onCaptured);

private:
    std::function<void (juce::KeyPress)> callback;
};

// Lists every shortcut: the User Keys' and the extra ones
class ShortcutsEditor : public juce::Component,
                        private juce::ListBoxModel
{
public:
    struct Host
    {
        std::function<int()> numUserKeys;
        std::function<juce::KeyPress (int)> getKeyShortcut;
        std::function<void (int, juce::KeyPress)> setKeyShortcut;
        std::function<juce::String (int)> describeKey;          // what the User Key does
        std::function<std::vector<Shortcut>&()> extras;
        std::function<void()> extrasChanged;
        std::function<juce::String (const Action&)> describeAction;
        std::function<void (std::function<void (Action)>)> chooseAction;
        std::function<void (const juce::String& forWhat, std::function<void (juce::KeyPress)>)> capture;
    };

    explicit ShortcutsEditor (Host);
    ~ShortcutsEditor() override;

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void changeKey (int row);

    Host host;
    juce::ListBox list { "Shortcuts", this };
    juce::TextButton addButton { "Add shortcut..." }, changeButton { "Change keys..." }, removeButton { "Remove" };
    juce::Label help;
};
