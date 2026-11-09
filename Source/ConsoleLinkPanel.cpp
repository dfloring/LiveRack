#include "ConsoleLinkPanel.h"

ConsoleLinkPanel::ConsoleLinkPanel (ConsoleLink& l, std::function<void()> applied)
    : link (l), onApplied (std::move (applied))
{
    const auto& s = link.getSettings();
    folder = s.sceneFolder;

    typeBox.addItem ("Off", 1);
    typeBox.addItem ("Yamaha CL/QL (RCP)", 2);
    typeBox.addItem ("Behringer X32 (OSC)", 3);
    typeBox.setSelectedId ((int) s.type + 1, juce::dontSendNotification);

    addressEditor.setText (s.address, false);
    addressEditor.setTextToShowWhenEmpty ("e.g. 192.168.0.128", juce::Colours::grey);

    folderPath.setText (folder.isDirectory() ? folder.getFullPathName() : "(none: snapshots are kept in the show)", juce::dontSendNotification);
    folderPath.setColour (juce::Label::textColourId, juce::Colours::lightgrey);

    followBox.addItem ("Scenes", 1);
    followBox.addItem ("Snippets", 2);
    followBox.addItem ("Cues", 3);
    followBox.setSelectedId (s.x32Follow + 1, juce::dontSendNotification);
    followBox.onChange = [this] { apply(); };
    learnButton.onClick = [this]
    {
        apply(); // make sure we're connected with the chosen mode
        link.startLearning();
    };
    triggerLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    triggerLabel.setFont (juce::FontOptions (12.0f));

    recallToggle.setToggleState (s.recallScenes, juce::dontSendNotification);
    followToggle.setToggleState (s.followSelection, juce::dontSendNotification);

    logView.setMultiLine (true);
    logView.setReadOnly (true);
    logView.setScrollbarsShown (true);
    logView.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));

    folderButton.onClick = [this] { chooseFolder(); };
    applyButton.onClick  = [this] { apply(); };

    for (auto* c : std::initializer_list<juce::Component*> {
             &typeLabel, &addressLabel, &folderLabel, &folderPath, &statusLabel, &logLabel,
             &typeBox, &addressEditor, &logView, &folderButton, &applyButton,
             &recallToggle, &followToggle, &followLabel, &followBox, &learnButton, &triggerLabel })
        addAndMakeVisible (c);

    startTimerHz (4);
    timerCallback();
    setSize (680, 560);
}

void ConsoleLinkPanel::resized()
{
    auto area = getLocalBounds().reduced (14);
    auto row = [&area] { auto r = area.removeFromTop (28); area.removeFromTop (8); return r; };

    auto r = row();
    typeLabel.setBounds (r.removeFromLeft (110));
    typeBox.setBounds (r.removeFromLeft (240));

    r = row();
    addressLabel.setBounds (r.removeFromLeft (110));
    addressEditor.setBounds (r.removeFromLeft (240));

    r = row();
    followLabel.setBounds (r.removeFromLeft (110));
    followBox.setBounds (r.removeFromLeft (140));
    r.removeFromLeft (8);
    learnButton.setBounds (r.removeFromLeft (80));
    r.removeFromLeft (10);
    triggerLabel.setBounds (r);

    r = row();
    folderLabel.setBounds (r.removeFromLeft (110));
    folderButton.setBounds (r.removeFromRight (100));
    r.removeFromRight (8);
    folderPath.setBounds (r);

    recallToggle.setBounds (row());
    followToggle.setBounds (row());

    r = row();
    applyButton.setBounds (r.removeFromLeft (120));
    r.removeFromLeft (12);
    statusLabel.setBounds (r);

    logLabel.setBounds (area.removeFromTop (22));
    logView.setBounds (area);
}

void ConsoleLinkPanel::timerCallback()
{
    statusLabel.setText (link.getStatusText(), juce::dontSendNotification);

    const bool isX32 = typeBox.getSelectedId() == 3;
    for (auto* c : std::initializer_list<juce::Component*> { &followLabel, &followBox, &learnButton, &triggerLabel })
        c->setEnabled (isX32);

    const auto& st = link.getSettings();
    triggerLabel.setText (link.isLearning() ? "Listening... recall one on the X32"
                                            : "Trigger: " + st.x32Trigger[juce::jlimit (0, 2, st.x32Follow)],
                          juce::dontSendNotification);

    if (link.getLogCounter() != shownLogCounter)
    {
        shownLogCounter = link.getLogCounter();
        logView.setText (link.getLog().joinIntoString ("\n"), false);
        logView.moveCaretToEnd();
    }
}

void ConsoleLinkPanel::apply()
{
    ConsoleLink::Settings s;
    s.type            = (ConsoleLink::Type) juce::jmax (0, typeBox.getSelectedId() - 1);
    s.address         = addressEditor.getText().trim();
    s.sceneFolder     = folder;
    s.recallScenes    = recallToggle.getToggleState();
    s.followSelection = followToggle.getToggleState();
    s.x32Follow       = juce::jmax (0, followBox.getSelectedId() - 1);

    // Keep learned trigger addresses
    for (int i = 0; i < 3; ++i)
        s.x32Trigger[i] = link.getSettings().x32Trigger[i];

    // Keep the name and card slots (new defaults if the console type changed)
    s.name = link.getSettings().name;
    if (s.type == link.getSettings().type)
        s.slots = link.getSettings().slots;

    link.setSettings (s);

    if (onApplied != nullptr)
        onApplied();
}

void ConsoleLinkPanel::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a folder of pre-0.13 scene files to import",
        folder.isDirectory() ? folder : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));

    juce::Component::SafePointer<ConsoleLinkPanel> safe (this);

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || ! fc.getResult().isDirectory())
                return;

            safe->folder = fc.getResult();
            safe->folderPath.setText (safe->folder.getFullPathName(), juce::dontSendNotification);
        });
}
