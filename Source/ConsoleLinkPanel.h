#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ConsoleLink.h"

// Settings + live message log for the console link
class ConsoleLinkPanel : public juce::Component,
                         private juce::Timer
{
public:
    ConsoleLinkPanel (ConsoleLink&, std::function<void()> onApplied);
    void resized() override;

private:
    void timerCallback() override;
    void apply();
    void chooseFolder();

    ConsoleLink& link;
    std::function<void()> onApplied;
    juce::File folder;

    juce::Label typeLabel    { {}, "Console" },
                addressLabel { {}, "IP address" },
                folderLabel  { {}, "Scene folder" },
                folderPath, statusLabel, logLabel { {}, "Messages from the console" };
    juce::ComboBox typeBox;
    juce::TextEditor addressEditor, logView;
    juce::TextButton folderButton { "Choose..." }, applyButton { "Connect" };
    juce::Label followLabel { {}, "X32 follows" }, triggerLabel;
    juce::ComboBox followBox;
    juce::TextButton learnButton { "Learn" };
    juce::ToggleButton recallToggle { "Recall a snapshot when the console recalls a scene" },
                       followToggle { "Select the matching rack when a channel is selected" };
    std::unique_ptr<juce::FileChooser> chooser;
    int shownLogCounter = -1;
};
