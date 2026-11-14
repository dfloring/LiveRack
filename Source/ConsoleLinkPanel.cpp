#include "ConsoleLinkPanel.h"

ConsoleLinkPanel::ConsoleLinkPanel (LiveConnection& l, int console)
    : live (l), index (console)
{
    const auto s = live.getConsole (index).settings;
    live.watchConsoleLog (index);   // a server sends this console's messages
    folder = s.sceneFolder;

    typeBox.addItem ("Off", 1);
    typeBox.addItem ("Yamaha CL/QL (RCP)", 2);
    typeBox.addItem ("Behringer X32 (OSC)", 3);
    typeBox.addItem ("Yamaha DM7 (RCP)", 4);
    typeBox.onChange = [this] { timerCallback(); resized(); };

    sideBox.addItem ("Scene list A", 1);
    sideBox.addItem ("Scene list B (Split mode)", 2);
    sideBox.setSelectedId (s.dm7Side + 1, juce::dontSendNotification);
    sideBox.onChange = [this] { apply(); };
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
        live.learnConsoleTrigger (index);
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
             &recallToggle, &followToggle, &followLabel, &followBox, &learnButton, &triggerLabel, &sideBox })
        addAndMakeVisible (c);

    // Importing old scene files reads a folder on the computer with the audio
    if (! live.isLocal())
    {
        folderButton.setEnabled (false);
        folderButton.setTooltip ("Choose this folder on " + live.getServerName() + " itself");
    }

    startTimerHz (4);
    timerCallback();
    setSize (680, 560);
}

ConsoleLinkPanel::~ConsoleLinkPanel()
{
    live.watchConsoleLog (-1);
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
    sideBox.setBounds (r.withWidth (240));
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
    const auto c = live.getConsole (index);
    statusLabel.setText (c.statusText, juce::dontSendNotification);

    const bool isX32 = typeBox.getSelectedId() == 3;
    const bool isDm7 = typeBox.getSelectedId() == 4;
    for (auto* c : std::initializer_list<juce::Component*> { &followBox, &learnButton, &triggerLabel })
    {
        c->setVisible (! isDm7);
        c->setEnabled (isX32);
    }
    sideBox.setVisible (isDm7);
    followLabel.setEnabled (isX32 || isDm7);
    followLabel.setText (isDm7 ? "DM7 follows" : "X32 follows", juce::dontSendNotification);

    const auto& st = c.settings;
    triggerLabel.setText (c.learning ? "Listening... recall one on the X32"
                                            : "Trigger: " + st.x32Trigger[juce::jlimit (0, 2, st.x32Follow)],
                          juce::dontSendNotification);

    // The log changed (or, from a server, has just arrived)
    const auto lines = live.getConsoleLog (index).joinIntoString ("\n");
    if (c.logCounter != shownLogCounter || lines != logView.getText())
    {
        shownLogCounter = c.logCounter;
        logView.setText (lines, false);
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
    s.dm7Side         = juce::jmax (0, sideBox.getSelectedId() - 1);

    const auto now = live.getConsole (index).settings;

    // Keep learned trigger addresses
    for (int i = 0; i < 3; ++i)
        s.x32Trigger[i] = now.x32Trigger[i];

    // Keep the name and card slots (new defaults if the console type changed)
    s.name = now.name;
    s.slots = s.type == now.type ? now.slots : ConsoleLink::defaultSlots (s.type);
    if (! live.isLocal())
        s.sceneFolder = now.sceneFolder;

    live.setConsoleSettings (index, s);
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
