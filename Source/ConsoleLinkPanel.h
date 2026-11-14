#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "LiveConnection.h"

// Settings + live message log for a console link, here or on a server
class ConsoleLinkPanel : public juce::Component,
                         private juce::Timer
{
public:
    ConsoleLinkPanel (LiveConnection&, int console);
    ~ConsoleLinkPanel() override;
    void resized() override;

private:
   #if LIVERACK_PREVIEW_HOOKS
    friend struct PreviewHooks;
   #endif
    void timerCallback() override;
    void apply();
    void chooseFolder();

    LiveConnection& live;
    const int index;
    juce::File folder;

    juce::Label typeLabel    { {}, "Console" },
                addressLabel { {}, "IP address" },
                folderLabel  { {}, "Old scenes" },
                folderPath, statusLabel, logLabel { {}, "Messages from the console" };
    juce::ComboBox typeBox;
    juce::TextEditor addressEditor, logView;
    juce::TextButton folderButton { "Choose..." }, applyButton { "Connect" };
    juce::Label followLabel { {}, "X32 follows" }, triggerLabel;
    juce::ComboBox followBox, sideBox;
    juce::TextButton learnButton { "Learn" };
    juce::ToggleButton recallToggle { "Recall a snapshot when the console recalls a scene" },
                       followToggle { "Select the matching rack when a channel is selected" };
    std::unique_ptr<juce::FileChooser> chooser;
    int shownLogCounter = -1;
};
