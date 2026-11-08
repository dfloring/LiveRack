#include "MainComponent.h"
#include "ConsoleLinkPanel.h"
#include "AppSettings.h"
#include "PluginPicker.h"
#include "ParameterInspector.h"
#include "RoutingMatrix.h"
#include "ScopeEditor.h"
#include "DevicesPanel.h"
#include "GainReduction.h"
#include <algorithm>

// Dims the window and shows the plugin picker in the middle;
// clicking outside the picker closes it
class MainComponent::PickerOverlay : public juce::Component
{
public:
    PickerOverlay (std::unique_ptr<PluginPicker> p, std::function<void()> onClose)
        : picker (std::move (p)), closeCallback (std::move (onClose))
    {
        addAndMakeVisible (*picker);
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black.withAlpha (0.5f)); }

    void resized() override
    {
        const int w = juce::jmin (getWidth() - 40, 1040), h = juce::jmin (getHeight() - 40, 700);
        picker->setBounds (getLocalBounds().withSizeKeepingCentre (w, h));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! picker->getBounds().contains (e.getPosition()))
            closeCallback();
    }

private:
    std::unique_ptr<PluginPicker> picker;
    std::function<void()> closeCallback;
};

// Window around a ParameterInspector
class MainComponent::InspectorWindow : public juce::DocumentWindow
{
public:
    InspectorWindow (const juce::String& title, ParameterInspector* content, std::function<void()> onClose)
        : DocumentWindow (title, Theme::background, DocumentWindow::closeButton),
          plugin (&content->getPlugin()), closeCallback (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (content, true);
        setResizable (true, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override { closeCallback(); }

    juce::AudioProcessor* const plugin;

private:
    std::function<void()> closeCallback;
};

// Second window holding the plugin panel ("rack view")
class MainComponent::RackViewWindow : public juce::DocumentWindow
{
public:
    explicit RackViewWindow (std::function<void()> onClose)
        : DocumentWindow ("LiveRack - Rack View", Theme::background, DocumentWindow::allButtons),
          closeCallback (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
    }

    void closeButtonPressed() override { closeCallback(); }

private:
    std::function<void()> closeCallback;
};

// Devices, Routing and Scope: windows that stay open while you work
class MainComponent::ToolWindow : public juce::DocumentWindow
{
public:
    ToolWindow (const juce::String& title, juce::Component* content, const juce::String& key, std::function<void()> onClose)
        : DocumentWindow (title, Theme::background, DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          settingsKey (key), closeCallback (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (content, true);
        setResizable (true, false);

        const auto saved = AppSettings::get (settingsKey);
        if (saved.isEmpty() || ! restoreWindowStateFromString (saved))
            centreWithSize (getWidth(), getHeight());

        setVisible (true);
    }

    ~ToolWindow() override { AppSettings::set (settingsKey, getWindowStateAsString()); }

    void closeButtonPressed() override { closeCallback(); }

private:
    juce::String settingsKey;
    std::function<void()> closeCallback;
};

// BPM read-out: click to type a tempo
class MainComponent::BpmDisplay : public juce::Component,
                                  public juce::SettableTooltipClient
{
public:
    explicit BpmDisplay (RackEngine& e) : engine (e) { setTooltip ("Tempo for tempo-synced plugins. Click to type, TAP (or T) to tap."); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (Theme::raised);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (Theme::border);
        g.drawRoundedRectangle (r, 10.0f, 1.0f);

        // Beat light
        const double beatMs = 60000.0 / engine.getBpm();
        const bool onBeat = std::fmod (juce::Time::getMillisecondCounterHiRes(), beatMs) < 90.0;
        Theme::drawLight (g, juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ r.getX() + 14.0f, r.getCentreY() }),
                          onBeat ? Theme::amber : Theme::border, onBeat);

        auto t = r.withTrimmedLeft (26.0f).reduced (4.0f, 3.0f);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText ("BPM", t.removeFromTop (12.0f).toNearestInt(), juce::Justification::centredLeft);
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (21.0f, juce::Font::bold));
        const double bpm = engine.getBpm();
        g.drawText (juce::String (bpm, std::abs (bpm - std::round (bpm)) < 0.05 ? 0 : 1),
                    t.toNearestInt(), juce::Justification::centredLeft);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        auto* w = new juce::AlertWindow ("Tempo", "Beats per minute for tempo-synced plugins.", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("bpm", juce::String (engine.getBpm(), 1));
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        auto& e = engine;
        w->enterModalState (true, juce::ModalCallbackFunction::create ([&e, w] (int r)
        {
            const double v = w->getTextEditorContents ("bpm").getDoubleValue();
            if (r == 1 && v > 0.0)
                e.setBpm (v);
        }), true);
    }

private:
    RackEngine& engine;
};

// Bottom bar: a chip per audio driver, then consoles and remote control
class MainComponent::StatusBar : public juce::Component
{
public:
    explicit StatusBar (MainComponent& m) : owner (m) {}

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1a1d21));
        g.setColour (Theme::border);
        g.fillRect (0, 0, getWidth(), 1);

        auto area = getLocalBounds().reduced (10, 4);
        auto& e = owner.engine;
        g.setFont (juce::FontOptions (12.0f));

        auto chip = [&g, &area] (juce::Colour light, const juce::String& bold, const juce::String& rest, bool fromRight)
        {
            const int w = juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (12.0f, juce::Font::bold), bold)
                        + juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (12.0f), rest) + 56;
            auto r = (fromRight ? area.removeFromRight (w) : area.removeFromLeft (w)).toFloat();
            if (fromRight) area.removeFromRight (8); else area.removeFromLeft (8);

            g.setColour (Theme::raised);
            g.fillRoundedRectangle (r, r.getHeight() / 2.0f);
            auto t = r.reduced (10.0f, 0.0f);
            Theme::drawLight (g, t.removeFromLeft (8.0f).withSizeKeepingCentre (8.0f, 8.0f), light, false);
            t.removeFromLeft (8.0f);
            g.setColour (Theme::text);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (bold, t.toNearestInt(), juce::Justification::centredLeft);
            t.removeFromLeft ((float) juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (12.0f, juce::Font::bold), bold) + 6.0f);
            g.setColour (Theme::textDim.brighter (0.2f));
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (rest, t.toNearestInt(), juce::Justification::centredLeft);
        };

        for (int d = 0; d < e.getNumDrivers(); ++d)
        {
            const auto s = e.getDriverStatus (d);
            if (! s.open)
            {
                chip (Theme::red, "Driver " + juce::String (d + 1), "no device: open Devices", false);
                continue;
            }

            const bool recentDrop = s.dropouts > 0 && juce::Time::getCurrentTime() - s.lastDropout < juce::RelativeTime::minutes (5);
            juce::String rest;
            rest << juce::String (s.sampleRate / 1000.0, 1) << " kHz  " << s.bufferSize << " smp  DSP "
                 << juce::String (s.cpu * 100.0, 0) << "% (pk " << juce::String (s.peakCpu * 100.0, 0) << "%)";
            if (s.dropouts >= 0)
            {
                rest << "  " << s.dropouts << (s.dropouts == 1 ? " drop" : " drops");
                if (s.dropouts > 0)
                    rest << " " << s.lastDropout.toString (false, true, false, true);
            }
            chip (recentDrop ? Theme::amber : Theme::green, s.deviceName.substring (0, 28), rest, false);
        }

        chip (owner.osc.isListening() ? (owner.osc.getNumClients() > 0 ? Theme::green : Theme::green.withAlpha (0.4f)) : Theme::textDim,
              "OSC", owner.osc.isEnabled() ? juce::String (owner.osc.getNumClients()) + " client" + (owner.osc.getNumClients() == 1 ? "" : "s") : "off", true);

        for (int i = (int) owner.consoles.size(); --i >= 0;)
        {
            auto& c = *owner.consoles[(size_t) i];
            if (c.getSettings().type == ConsoleLink::Type::off)
                continue;
            chip (c.isConnected() ? Theme::green : Theme::red, c.getDisplayName(),
                  c.isConnected() ? (c.getCurrentScene() >= 0 ? "scene " + juce::String (c.getCurrentScene()) : juce::String ("linked")) : "not connected", true);
        }

        if (owner.midi != nullptr && owner.midi->isLearning())
        {
            g.setColour (Theme::amber);
            g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
            g.drawText (owner.midi->getLearnText(), area, juce::Justification::centredLeft, true);
        }
        else if (owner.consoleNote.isNotEmpty())
        {
            g.setColour (Theme::textDim);
            g.drawText (owner.consoleNote, area, juce::Justification::centredLeft, true);
        }
    }

    void mouseUp (const juce::MouseEvent&) override { owner.showDevices(); }

private:
    MainComponent& owner;
};

#if LIVERACK_PREVIEW_HOOKS
 #include "PreviewHooks.h"
#endif

//==============================================================================
MainComponent::MainComponent()
{
    bpmDisplay = std::make_unique<BpmDisplay> (engine);
    statusBar = std::make_unique<StatusBar> (*this);

    for (auto* b : { &devicesButton, &routingButton, &scopeButton, &scanButton, &storeSceneButton,
                     &addRackButton, &windowModeButton, &stackRowsButton, &saveButton, &loadButton, &tapButton })
        addAndMakeVisible (b);

    addAndMakeVisible (*bpmDisplay);
    addAndMakeVisible (hotBar);
    addAndMakeVisible (pageTabs);
    addAndMakeVisible (strips);
    addAndMakeVisible (dock);
    addAndMakeVisible (keysBar);
    addAndMakeVisible (*statusBar);

    devicesButton.onClick    = [this] { showDevices(); };
    routingButton.onClick    = [this] { showRouting (selectedRack); };
    scopeButton.onClick      = [this] { showScope(); };
    scanButton.onClick       = [this] { showPluginScanner(); };
    storeSceneButton.onClick = [this] { showStoreSceneDialog(); };
    addRackButton.onClick    = [this] { addRack(); };
    windowModeButton.onClick = [this] { setTwoWindowMode (! isTwoWindowMode()); };
    tapButton.onClick        = [this] { engine.tapTempo(); };
    tapButton.setColour (juce::TextButton::buttonColourId, Theme::accentDeep);

    devicesButton.setTooltip ("Audio drivers, consoles and controllers");
    routingButton.setTooltip ("Patch rack inputs and outputs");
    scopeButton.setTooltip ("What snapshots recall");
    scanButton.setTooltip ("Scan for plugins");

    const bool stack = AppSettings::get ("stackRows") != "0"; // on by default
    stackRowsButton.setClickingTogglesState (true);
    stackRowsButton.setToggleState (stack, juce::dontSendNotification);
    strips.setStackRows (stack);
    stackRowsButton.onClick = [this]
    {
        const bool on = stackRowsButton.getToggleState();
        strips.setStackRows (on);
        AppSettings::set ("stackRows", on ? "1" : "0");
    };
    saveButton.onClick = [this] { saveSnapshot(); };
    loadButton.onClick = [this] { loadSnapshot(); };
    saveButton.setTooltip ("Save a snapshot file");
    loadButton.setTooltip ("Load a snapshot file");

    strips.onSelect    = [this] (int r, int s) { select (r, s); };
    strips.onAddPlugin = [this] (int r) { showAddPluginMenu (r); };
    strips.onSlotMenu  = [this] (int r, int s) { showSlotMenu (r, s); };
    strips.onRackMenu  = [this] (int r) { showRackMenu (r); };
    strips.onEditRack  = [this] (int r) { showRackDialog (r); };
    strips.onAddRack   = [this] { addRack(); };
    strips.onOpenRouting = [this] (int r) { showRouting (r); };
    strips.onEditConsoleChannel = [this] (int r) { editConsoleChannel (r); };
    strips.onFormatChange = [this] (int r, const RackConfig& c) { changeFormat (r, c); };
    strips.onSlotDropped = [this] (int fr, int fs, int tr, int ti, bool copy) { handleSlotDrop (fr, fs, tr, ti, copy); };
    strips.onPluginDropped = [this] (const juce::String& id, int tr, int ti) { handlePluginDrop (id, tr, ti); };
    strips.setLibrary (&library);

    dock.onSelectSlot   = [this] (int r, int s) { select (r, s); };
    dock.onPopOut       = [this] (int r, int s) { popOutEditor (r, s); };
    dock.onRemove       = [this] (int r, int s) { removeSlot (r, s); };
    dock.onToggleBypass = [this] (int r, int s) { engine.setSlotBypassed (r, s, ! engine.isSlotBypassed (r, s)); };
    dock.onPreferredHeightChanged = [this] { resized(); };
    dock.onSlotDropped = [this] (int fr, int fs, int tr, int ti, bool copy) { handleSlotDrop (fr, fs, tr, ti, copy); };

    hotBar.onOpen = [this] (int id) { openSlotId (id); };
    pageTabs.onAddPage = [this] { addPage(); };
    pageTabs.onPageMenu = [this] (int p) { showPageMenu (p); };

    midi = std::make_unique<MidiControl> (engine, *this);
    midi->onChanged = [this] { statusBar->repaint(); };
    keysBar.onMidiLearn = [this] (int key) { midi->learnAction (keysBar.getKey (key)); };
    keysBar.onSetShortcut = [this] (int key)
    {
        captureShortcut ("User Key " + juce::String (key + 1), [this, key] (juce::KeyPress k) { keysBar.setShortcut (key, k); });
    };

    osc.onMessage = [this] (const juce::String& a, const juce::Array<juce::var>& args) { handleOsc (a, args); };
    webRemote.buildState = [this] (const juce::String& q) { return buildWebState (q); };
    webRemote.onCommand  = [this] (const juce::var& c) { handleWebCommand (c); };

    // Consoles: console-link.xml, console-link-2.xml, ...
    const int numConsoles = juce::jmax (1, AppSettings::get ("consoles").getIntValue());
    for (int i = 0; i < numConsoles; ++i)
    {
        consoles.push_back (std::make_unique<ConsoleLink>());
        consoles.back()->loadSettingsFrom (consoleSettingsFile (i));
        wireConsole (i);
    }

    engine.getKnownPlugins().addChangeListener (this);
    engine.addChangeListener (this);
    refreshPluginList();

    // Bring back whatever was running when the app last quit
    juce::String error;
    const bool hadSession = engine.restoreSession (error);

    if (error.isNotEmpty())
        showError ("Last session restored with problems", error);

    if (! hadSession && engine.getNumRacks() == 0)
    {
        RackConfig first;
        first.name = "Rack 1";
        first.stereo = true;
        engine.addRack (first);
    }

    addKeyListener (this);
    setWantsKeyboardFocus (true);

    select (0, 0);
    startTimerHz (30);
    setSize (1440, 940);

    if (AppSettings::get ("twoWindows") == "1")
    {
        juce::Component::SafePointer<MainComponent> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->setTwoWindowMode (true); });
    }

   #if LIVERACK_PREVIEW_HOOKS
    juce::Timer::callAfterDelay (300, [this] { PreviewHooks::run (*this); });
   #endif
}

MainComponent::~MainComponent()
{
    stopTimer();
    removeKeyListener (this);
    engine.getKnownPlugins().removeChangeListener (this);
    engine.removeChangeListener (this);
    osc.onMessage = nullptr;
    webRemote.setEnabled (false, webRemote.getPort());
    webRemote.buildState = nullptr;
    webRemote.onCommand = nullptr;

    for (auto& c : consoles)
    {
        c->onSceneRecalled = nullptr;
        c->onChannelSelected = nullptr;
    }
    saveConsoles(); // keeps anything learned
    keysBar.save();

    devicesWindow.reset();
    shortcutsWindow.reset();
    routingWindow.reset();
    scopeWindow.reset();
    rackDialog.reset();
    sceneDialog.reset();
    pickerOverlay.reset();
    closeAllEditors(); // editors must die before their plugins
    midi.reset();

    if (rackWindow != nullptr)
    {
        AppSettings::set ("rackWindow", rackWindow->getWindowStateAsString());
        rackWindow->clearContentComponent();
        rackWindow.reset();
    }
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    // Toolbar background and logo
    g.setColour (juce::Colour (0xff1a1d21));
    g.fillRect (0, 0, getWidth(), 64);
    g.setColour (Theme::border);
    g.fillRect (0, 64, getWidth(), 1);

    auto logo = juce::Rectangle<float> (16.0f, 16.0f, 32.0f, 32.0f);
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (logo, 7.0f);
    g.setColour (juce::Colour (0xff3a4048));
    g.drawRoundedRectangle (logo, 7.0f, 1.0f);
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (i == 1 ? Theme::accent : juce::Colour (0xff3a4048));
        g.fillRoundedRectangle (logo.getX() + 6.0f, logo.getY() + 6.5f + i * 7.0f, 20.0f, 4.5f, 1.0f);
    }
    g.setColour (Theme::amber);
    g.fillRoundedRectangle (logo.getX() + 18.0f, logo.getY() + 14.0f, 6.0f, 2.0f, 1.0f);

    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
    g.drawText ("LiveRack", 56, 0, 100, 64, juce::Justification::centredLeft);

    // Scene chip
    auto chip = juce::Rectangle<float> (158.0f, 18.0f, 210.0f, 28.0f);
    g.setColour (Theme::raised);
    g.fillRoundedRectangle (chip, 14.0f);
    auto* console = primaryConsole();
    const bool linked = console != nullptr && console->isConnected();
    Theme::drawLight (g, juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ chip.getX() + 15.0f, chip.getCentreY() }),
                      linked ? Theme::green : Theme::textDim, linked);
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (currentScene >= 0 ? "Scene " + juce::String (currentScene) : juce::String ("No scene recalled"),
                chip.withTrimmedLeft (28.0f).toNearestInt(), juce::Justification::centredLeft);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    auto bar = area.removeFromTop (64).reduced (12, 10);
    bar.removeFromLeft (370); // logo + scene chip

    tapButton.setBounds (bar.removeFromRight (64));
    bar.removeFromRight (4);
    bpmDisplay->setBounds (bar.removeFromRight (92));
    bar.removeFromRight (16);

    for (auto* b : { &loadButton, &saveButton })
    {
        b->setBounds (bar.removeFromRight (70));
        bar.removeFromRight (6);
    }
    bar.removeFromRight (10);
    stackRowsButton.setBounds (bar.removeFromRight (108)); bar.removeFromRight (6);
    windowModeButton.setBounds (bar.removeFromRight (124)); bar.removeFromRight (6);
    addRackButton.setBounds (bar.removeFromRight (84));

    for (auto* b : { &devicesButton, &routingButton, &scopeButton, &scanButton, &storeSceneButton })
    {
        b->setBounds (bar.removeFromLeft (b == &storeSceneButton ? 112 : 92));
        bar.removeFromLeft (6);
    }

    statusBar->setBounds (area.removeFromBottom (30));

    area.reduce (12, 8);
    hotBar.setBounds (area.removeFromTop (HotPluginsBar::preferredHeight));
    area.removeFromTop (6);
    pageTabs.setBounds (area.removeFromTop (PageTabs::preferredHeight));
    area.removeFromTop (2);
    keysBar.setBounds (area.removeFromBottom (UserKeysBar::preferredHeight));
    area.removeFromBottom (8);

    if (pickerOverlay != nullptr)
        pickerOverlay->setBounds (getLocalBounds());

    if (isTwoWindowMode())
    {
        strips.setBounds (area); // the plugin panel lives in the rack view window
        return;
    }

    // Give the plugin editor its full height; the strips take what's left
    const int stripsHeight = juce::jlimit (300, 470, area.getHeight() - dock.getPreferredHeight() - 8);
    strips.setBounds (area.removeFromTop (stripsHeight));
    area.removeFromTop (8);
    dock.setBounds (area);
}

//==============================================================================
void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &engine.getKnownPlugins())
    {
        refreshPluginList();
        engine.saveSettings();
    }
    else
    {
        refreshAll();
    }
}

void MainComponent::timerCallback()
{
    strips.updateMeters();
    engine.updateDriverStats();
    ++ticks;

    bpmDisplay->repaint();

    if (ticks % 3 == 0)
    {
        keysBar.repaint();
        sendFeedback();
    }

    if (ticks % 8 == 0)
    {
        statusBar->repaint();
        repaint (0, 0, 380, 64); // scene chip
    }
}

bool MainComponent::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    // User Key shortcuts first, then the extra ones
    if (const int k = keysBar.keyForShortcut (key); k >= 0)
    {
        keysBar.press (k);
        return true;
    }

    for (auto& sc : extraShortcuts)
        if (sc.key == key)
        {
            performAction (sc.action, -1);
            return true;
        }

    return false;
}

//==============================================================================
// Actions
void MainComponent::performAction (const Action& a, int value)
{
    auto rackOf = [this] (int r) { return r < 0 ? selectedRack : r; };

    auto slotOf = [this] (int slotId, int& r, int& s)
    {
        if (slotId <= 0) { r = selectedRack; s = selectedSlot; return engine.getPlugin (r, s) != nullptr; }
        return engine.findSlot (slotId, r, s);
    };

    switch (a.type)
    {
        case Action::rackBypass:
        {
            const int r = rackOf (a.rack);
            if (juce::isPositiveAndBelow (r, engine.getNumRacks()))
                engine.setRackBypassed (r, value < 0 ? ! engine.isRackBypassed (r) : value > 0);
            break;
        }
        case Action::pluginBypass:
        {
            int r, s;
            if (slotOf (a.slotId, r, s))
                engine.setSlotBypassed (r, s, value < 0 ? ! engine.isSlotBypassed (r, s) : value > 0);
            break;
        }
        case Action::selectRack:
            if (juce::isPositiveAndBelow (a.rack, engine.getNumRacks()))
                select (a.rack, -1);
            break;
        case Action::openPlugin:   openSlotId (a.slotId); break;
        case Action::hotPlugin:
        {
            const auto& hot = engine.getHotPlugins();
            if (juce::isPositiveAndBelow (a.number - 1, (int) hot.size()))
                openSlotId (hot[(size_t) a.number - 1]);
            break;
        }
        case Action::recallScene:  recallScene (a.number); break;
        case Action::nextScene:    stepScene (1); break;
        case Action::prevScene:    stepScene (-1); break;
        case Action::tapTempo:     engine.tapTempo(); break;
        case Action::twoWindows:   setTwoWindowMode (value < 0 ? ! isTwoWindowMode() : value > 0); break;
        case Action::storeScene:   storeScene (juce::jmax (0, currentScene)); break;
        case Action::showPage:     engine.setCurrentPage (a.number - 1); break;
        case Action::none: case Action::numTypes: default: break;
    }

    keysBar.repaint();
    hotBar.repaint();
}

bool MainComponent::isActionActive (const Action& a) const
{
    switch (a.type)
    {
        case Action::rackBypass:
            return engine.isRackBypassed (a.rack < 0 ? selectedRack : a.rack);
        case Action::pluginBypass:
        {
            int r = selectedRack, s = selectedSlot;
            if (a.slotId > 0 && ! engine.findSlot (a.slotId, r, s))
                return false;
            return engine.isSlotBypassed (r, s);
        }
        case Action::selectRack:   return a.rack == selectedRack;
        case Action::openPlugin:
        {
            int r, s;
            return engine.findSlot (a.slotId, r, s) && r == selectedRack && s == selectedSlot;
        }
        case Action::hotPlugin:
        {
            const auto& hot = engine.getHotPlugins();
            int r, s;
            return juce::isPositiveAndBelow (a.number - 1, (int) hot.size())
                   && engine.findSlot (hot[(size_t) a.number - 1], r, s) && r == selectedRack && s == selectedSlot;
        }
        case Action::recallScene:  return currentScene == a.number;
        case Action::twoWindows:   return isTwoWindowMode();
        case Action::showPage:     return engine.getCurrentPage() == a.number - 1;
        case Action::tapTempo:     return std::fmod (juce::Time::getMillisecondCounterHiRes(), 60000.0 / engine.getBpm()) < 100.0;
        case Action::nextScene: case Action::prevScene: case Action::storeScene:
        case Action::none: case Action::numTypes: default:
            return false;
    }
}

void MainComponent::openSlotId (int slotId)
{
    int r, s;
    if (! engine.findSlot (slotId, r, s))
        return;

    select (r, s);
    if (rackWindow != nullptr)
        rackWindow->toFront (false);
}

//==============================================================================
// OSC remote
void MainComponent::handleOsc (const juce::String& address, const juce::Array<juce::var>& args)
{
    juce::StringArray p;
    p.addTokens (address, "/", "");
    p.removeEmptyStrings();

    if (p.size() < 2 || p[0] != "liverack")
        return;

    const int value = args.isEmpty() ? -1 : ((int) args[0] != 0 ? 1 : 0);
    const int argInt = args.isEmpty() ? 0 : (int) args[0];

    auto make = [] (Action::Type t, int rack = -1, int slotId = 0, int number = 0)
    {
        Action a; a.type = t; a.rack = rack; a.slotId = slotId; a.number = number; return a;
    };

    if (p[1] == "rack" && p.size() >= 4)
    {
        const int r = p[2].getIntValue() - 1; // 1-based in OSC
        if (p[3] == "bypass")  performAction (make (Action::rackBypass, r), value);
        if (p[3] == "select")  performAction (make (Action::selectRack, r), -1);
        if (p[3] == "slot" && p.size() >= 6 && p[5] == "bypass")
            performAction (make (Action::pluginBypass, -1, engine.getSlotId (r, p[4].getIntValue() - 1)), value);
    }
    else if (p[1] == "selected" && p.size() >= 3 && p[2] == "bypass")
        performAction (make (Action::rackBypass, -1), value);
    else if (p[1] == "key" && p.size() >= 4 && p[3] == "press")
        keysBar.press (p[2].getIntValue() - 1);
    else if (p[1] == "hot" && p.size() >= 4 && p[3] == "open")
        performAction (make (Action::hotPlugin, -1, 0, p[2].getIntValue()), -1);
    else if (p[1] == "scene" && p.size() >= 3)
    {
        if (p[2] == "recall") recallScene (argInt);
        if (p[2] == "next")   stepScene (1);
        if (p[2] == "prev")   stepScene (-1);
        if (p[2] == "store")  storeScene (args.isEmpty() ? juce::jmax (0, currentScene) : argInt);
    }
    else if (p[1] == "page" && p.size() >= 3)
    {
        if (p[2] == "next")      engine.setCurrentPage (engine.getCurrentPage() + 1 >= (int) engine.getPages().size() ? -1 : engine.getCurrentPage() + 1);
        else if (p[2] == "prev") engine.setCurrentPage (engine.getCurrentPage() <= -1 ? (int) engine.getPages().size() - 1 : engine.getCurrentPage() - 1);
        else if (p.size() >= 4 && p[3] == "select") engine.setCurrentPage (p[2].getIntValue() - 1); // 0 = All
    }
    else if (p[1] == "tap")
        engine.tapTempo();
    else if (p[1] == "bpm" && ! args.isEmpty())
        engine.setBpm ((double) args[0]);
    else if (p[1] == "view" && p.size() >= 3 && p[2] == "twowindows")
        performAction (make (Action::twoWindows), value);
}

// State to OSC clients (only values that changed are actually sent)
void MainComponent::sendFeedback()
{
    if (! osc.isListening() || osc.getNumClients() == 0)
        return;

    osc.send ("/liverack/racks", engine.getNumRacks());
    osc.send ("/liverack/selected", selectedRack + 1);
    osc.send ("/liverack/scene", currentScene);
    osc.send ("/liverack/bpm", engine.getBpm());
    osc.send ("/liverack/page", engine.getCurrentPage() + 1); // 0 = All
    osc.send ("/liverack/pages", (int) engine.getPages().size());
    for (int i = 0; i < (int) engine.getPages().size(); ++i)
        osc.send ("/liverack/page/" + juce::String (i + 1) + "/name", engine.getPages()[(size_t) i].name);

    for (int r = 0; r < engine.getNumRacks(); ++r)
    {
        const auto base = "/liverack/rack/" + juce::String (r + 1) + "/";
        const auto cfg = engine.getRackConfig (r);
        osc.send (base + "name", cfg.name);
        osc.send (base + "bypass", engine.isRackBypassed (r) ? 1 : 0);
        osc.send (base + "colour", RackStripsView::colourFor (engine, r).toDisplayString (false));
        osc.send (base + "format", cfg.formatName());
        osc.send (base + "selected", r == selectedRack ? 1 : 0);
        osc.send (base + "group", RackConfig::groupName (cfg.latencyGroup));
        osc.send (base + "latency", std::round (engine.getRackTotalLatencyMs (r) * 100.0) / 100.0);
        osc.send (base + "gr", std::round (strips.getMaxGainReduction (r) * 2.0) / 2.0);

        for (int s = 0; s < engine.getNumSlots (r); ++s)
        {
            const auto sb = base + "slot/" + juce::String (s + 1) + "/";
            if (auto* p = engine.getPlugin (r, s))
                osc.send (sb + "name", p->getName());
            osc.send (sb + "bypass", engine.isSlotBypassed (r, s) ? 1 : 0);
        }
    }

    for (int k = 0; k < UserKeysBar::numKeys; ++k)
    {
        const auto& a = keysBar.getKey (k);
        osc.send ("/liverack/key/" + juce::String (k + 1) + "/label", a.type == Action::none ? juce::String() : a.describe (engine));
        osc.send ("/liverack/key/" + juce::String (k + 1) + "/state", a.type == Action::tapTempo ? 0 : (isActionActive (a) ? 1 : 0));
    }

    const auto& hot = engine.getHotPlugins();
    for (int i = 0; i < RackEngine::maxHotPlugins; ++i)
    {
        juce::String name;
        int r, s;
        if (i < (int) hot.size() && engine.findSlot (hot[(size_t) i], r, s))
            if (auto* p = engine.getPlugin (r, s))
                name = engine.getRackConfig (r).name + " " + p->getName();
        osc.send ("/liverack/hot/" + juce::String (i + 1) + "/name", name);
    }

    for (int d = 0; d < engine.getNumDrivers(); ++d)
    {
        const auto st = engine.getDriverStatus (d);
        const auto base = "/liverack/driver/" + juce::String (d + 1) + "/";
        osc.send (base + "name", st.deviceName);
        osc.send (base + "dsp", (int) std::round (st.cpu * 100.0));
        osc.send (base + "dropouts", st.dropouts);
    }
}

void MainComponent::dragOperationStarted (const juce::DragAndDropTarget::SourceDetails& d)
{
    // Dragging out of the plugin picker: get it out of the way so the
    // strips can take the drop
    if (d.description.toString().startsWith ("plugin:") && pickerOverlay != nullptr)
        pickerOverlay->setVisible (false);
}

void MainComponent::dragOperationEnded (const juce::DragAndDropTarget::SourceDetails& d)
{
    if (d.description.toString().startsWith ("plugin:") && pickerOverlay != nullptr)
        closePicker();
}

//==============================================================================
// Selection
void MainComponent::select (int rack, int slot)
{
    if (slot < 0)
        slot = (rack == selectedRack) ? selectedSlot : 0;

    selectedRack = rack;
    selectedSlot = slot;
    showRackOnSomePage (rack);
    refreshAll();
    strips.scrollToRack (selectedRack);

    if (routingMatrix != nullptr)
        routingMatrix->setSelectedRack (rack);
}

void MainComponent::refreshAll()
{
    const int n = engine.getNumRacks();

    if (n == 0)
    {
        selectedRack = selectedSlot = -1;
    }
    else
    {
        selectedRack = juce::jlimit (0, n - 1, juce::jmax (0, selectedRack));
        const int slots = engine.getNumSlots (selectedRack);
        selectedSlot = slots == 0 ? -1 : juce::jlimit (0, slots - 1, juce::jmax (0, selectedSlot));
    }

    strips.setSelection (selectedRack, selectedSlot);
    strips.refresh();
    hotBar.repaint();
    pageTabs.repaint();
    if (shortcutsEditor != nullptr) shortcutsEditor->refresh();
    keysBar.repaint();

    if (routingMatrix != nullptr) routingMatrix->refresh();
    if (scopeEditor != nullptr)   scopeEditor->refresh();

    // While the plugin picker is open the docked plugin stays closed
    // (its native view would draw over the picker)
    if (pickerOverlay == nullptr)
        dock.show (selectedRack, selectedSlot);
}

void MainComponent::refreshPluginList()
{
    library.rebuild (engine.getKnownPlugins().getTypes());
}

//==============================================================================
// Racks
void MainComponent::addRack()
{
    // Next free channels after the last rack on driver 1, patched in Routing
    int next = 0;
    for (int i = 0; i < engine.getNumRacks(); ++i)
    {
        const auto c = engine.getRackConfig (i);
        if (c.driver == 0)
            for (int ch = 0; ch < c.numChannels(); ++ch)
                next = juce::jmax (next, c.input[ch] + 1);
    }

    RackConfig cfg;
    cfg.name = "Rack " + juce::String (engine.getNumRacks() + 1);
    cfg.stereo = false;
    cfg.patchContiguous (next, next);
    const int r = engine.addRack (cfg);
    if (engine.getCurrentPage() >= 0)
        engine.setRackOnPage (r, engine.getCurrentPage(), true); // new racks join the page you're on
    select (r, -1);
}

void MainComponent::showRackDialog (int rackIndex)
{
    if (! juce::isPositiveAndBelow (rackIndex, engine.getNumRacks()))
        return;

    const auto cfg = engine.getRackConfig (rackIndex);

    rackDialog = std::make_unique<juce::AlertWindow> ("Rack",
        "Routing is in Routing; format, latency group, Recall Safe and colour are the badges on the strip.",
        juce::MessageBoxIconType::NoIcon);

    rackDialog->addTextEditor ("name", cfg.name, "Name");
    rackDialog->addTextEditor ("console", juce::String (cfg.consoleChannel), "Console channel (0 = none)");

    if (consoles.size() > 1)
    {
        juce::StringArray names;
        for (auto& c : consoles) names.add (c->getDisplayName());
        rackDialog->addComboBox ("whichConsole", names, "On console");
        rackDialog->getComboBoxComponent ("whichConsole")->setSelectedItemIndex (juce::jlimit (0, names.size() - 1, cfg.console));
    }

    rackDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    rackDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MainComponent> safe (this);
    rackDialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe, rackIndex] (int result)
    {
        if (safe != nullptr)
            safe->finishRackDialog (result, rackIndex);
    }), false);
}

void MainComponent::finishRackDialog (int result, int rackIndex)
{
    if (rackDialog != nullptr && result == 1 && juce::isPositiveAndBelow (rackIndex, engine.getNumRacks()))
    {
        auto cfg = engine.getRackConfig (rackIndex);
        cfg.name = rackDialog->getTextEditorContents ("name").trim();
        if (cfg.name.isEmpty())
            cfg.name = "Rack";
        cfg.consoleChannel = juce::jmax (0, rackDialog->getTextEditorContents ("console").getIntValue());
        if (auto* box = rackDialog->getComboBoxComponent ("whichConsole"))
            cfg.console = juce::jmax (0, box->getSelectedItemIndex());
        engine.setRackConfig (rackIndex, cfg);
    }

    juce::Component::SafePointer<MainComponent> safe (this);
    juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->rackDialog.reset(); });
}

void MainComponent::editConsoleChannel (int rack)
{
    showRackDialog (rack);
}

void MainComponent::changeFormat (int rack, const RackConfig& cfg)
{
    if (engine.getNumSlots (rack) == 0)
    {
        engine.setRackConfig (rack, cfg);
        return;
    }

    juce::Component::SafePointer<MainComponent> safe (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon,
        cfg.stereo ? "Make this rack stereo?" : "Make this rack mono?",
        "Its plugins are switched over and re-prepared, so this rack drops out for a moment.",
        cfg.stereo ? "Make stereo" : "Make mono", "Cancel", nullptr,
        juce::ModalCallbackFunction::create ([safe, rack, cfg] (int r)
        {
            if (safe != nullptr && r == 1)
            {
                safe->dock.releaseEditor();
                safe->closeEditorsForRack (rack);
                safe->engine.setRackConfig (rack, cfg);
                safe->refreshAll();
            }
        }));
}

void MainComponent::showRackMenu (int rack)
{
    const auto cfg = engine.getRackConfig (rack);

    juce::PopupMenu m;
    m.addSectionHeader (cfg.name);
    m.addItem (1, "Name and console channel...");
    m.addItem (5, "Routing...");
    m.addItem (2, "Bypass", true, engine.isRackBypassed (rack));
    m.addItem (3, "Recall safe", true, cfg.recallSafe);

    juce::PopupMenu groups;
    groups.addItem (200, "None", true, cfg.latencyGroup == 0);
    for (int g = 1; g <= 8; ++g)
        groups.addColouredItem (200 + g, "Group " + RackConfig::groupName (g), Theme::groupColour (g), true, cfg.latencyGroup == g);
    m.addSubMenu ("Latency group", groups);

    juce::PopupMenu pagesMenu;
    for (int p = 0; p < (int) engine.getPages().size(); ++p)
        pagesMenu.addItem (300 + p, engine.getPages()[(size_t) p].name, true, engine.isRackOnPage (rack, p));
    if (! engine.getPages().empty())
        pagesMenu.addSeparator();
    pagesMenu.addItem (299, "New page with this rack...");
    m.addSubMenu ("Pages", pagesMenu);

    juce::PopupMenu colours;
    colours.addItem (99, "Automatic", true, cfg.colourIndex < 0);
    for (int i = 0; i < RackStripsView::numColours; ++i)
        colours.addColouredItem (100 + i, RackStripsView::colourName (i), RackStripsView::colourForIndex (i),
                                 true, cfg.colourIndex == i);
    m.addSubMenu ("Colour", colours);

    m.addSeparator();
    m.addItem (4, "Remove rack");

    juce::Component::SafePointer<MainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, rack] (int result)
    {
        if (safe == nullptr)
            return;

        auto& e = safe->engine;
        switch (result)
        {
            case 1: safe->showRackDialog (rack); break;
            case 2: e.setRackBypassed (rack, ! e.isRackBypassed (rack)); break;
            case 3: e.setRackRecallSafe (rack, ! e.getRackConfig (rack).recallSafe); break;
            case 4: safe->removeRack (rack); break;
            case 5: safe->showRouting (rack); break;
            case 99: e.setRackColour (rack, -1); break;
            default:
                if (result >= 100 && result < 100 + RackStripsView::numColours)
                    e.setRackColour (rack, result - 100);
                if (result >= 200 && result <= 208)
                    e.setRackLatencyGroup (rack, result - 200);
                if (result == 299)
                {
                    safe->selectedRack = rack;
                    safe->addPage();
                }
                if (result >= 300 && result < 300 + (int) e.getPages().size())
                    e.setRackOnPage (rack, result - 300, ! e.isRackOnPage (rack, result - 300));
                break;
        }
    });
}

void MainComponent::removeRack (int rack)
{
    if (! juce::isPositiveAndBelow (rack, engine.getNumRacks()))
        return;

    dock.releaseEditor();
    closeEditorsForRack (rack);
    engine.removeRack (rack);
    select (juce::jmin (rack, engine.getNumRacks() - 1), 0);
}

//==============================================================================
// Plugins
void MainComponent::showAddPluginMenu (int rack)
{
    if (library.getFamilies().empty())
    {
        showError ("No plugins yet", "Use Plugins first to scan for your plugins.");
        return;
    }

    juce::Component::SafePointer<MainComponent> safe (this);

    const auto cfg = engine.getRackConfig (rack);
    const int end = engine.getNumSlots (rack);
    juce::String rackText = cfg.stereo ? "stereo rack" : "mono rack";
    if (cfg.isMonoToStereo())
        rackText = engine.getStereoPoint (rack) >= 0 ? "mono-to-stereo rack, stereo from here" : "mono-to-stereo rack, still mono";

    auto picker = std::make_unique<PluginPicker> (library,
        [safe, rack, end] (const PluginLibrary::Family& f)
        {
            return safe != nullptr ? safe->wantForSlot (rack, end, f) : PluginLibrary::Want::mono;
        },
        rackText,
        [safe, rack] (const juce::PluginDescription& desc)
        {
            if (safe == nullptr)
                return;

            juce::String error;
            if (safe->engine.addPlugin (rack, desc, error))
            {
                const int slot = safe->engine.getNumSlots (rack) - 1;

                // A flexible effect added where a mono-to-stereo rack is still
                // mono becomes its stereo point
                if (safe->engine.getRackConfig (rack).isMonoToStereo() && safe->engine.getStereoPoint (rack) < 0
                    && PluginLibrary::versionLabel (desc).isEmpty()
                    && PluginLibrary::suitsStereoPoint (safe->library.categoryFor (desc)))
                    safe->engine.setStereoPoint (rack, slot);

                safe->select (rack, slot);
            }
            else
                safe->showError ("Couldn't load plugin", error);
        });

    auto close = [safe] { if (safe != nullptr) safe->closePicker(); };

    picker->onClose = close;
    pickerOverlay = std::make_unique<PickerOverlay> (std::move (picker), close);
    dock.releaseEditor(); // native plugin views would draw over the picker

    addAndMakeVisible (*pickerOverlay);
    pickerOverlay->setBounds (getLocalBounds());
}

void MainComponent::closePicker()
{
    juce::Component::SafePointer<MainComponent> safe (this);
    juce::MessageManager::callAsync ([safe]
    {
        if (safe == nullptr)
            return;

        safe->pickerOverlay.reset();
        safe->refreshAll(); // reopens the docked plugin window
    });
}

void MainComponent::handlePluginDrop (const juce::String& pluginId, int toRack, int toIndex)
{
    if (! juce::isPositiveAndBelow (toRack, engine.getNumRacks()))
        return;

    juce::PluginDescription desc;

    if (pluginId.startsWith ("family:"))
    {
        auto* f = library.findFamily (pluginId.fromFirstOccurrenceOf ("family:", false, false));
        if (f == nullptr)
            return;
        desc = PluginLibrary::bestVersionFor (*f, wantForSlot (toRack, toIndex, *f));
        library.noteUsed (f->key);
    }
    else if (auto type = engine.getKnownPlugins().getTypeForIdentifierString (pluginId.fromFirstOccurrenceOf ("id:", false, false)))
    {
        desc = *type;
    }
    else
    {
        return;
    }

    juce::String error;
    if (! engine.addPlugin (toRack, desc, error))
    {
        showError ("Couldn't load plugin", error);
        return;
    }

    const int last = engine.getNumSlots (toRack) - 1;
    engine.moveSlotTo (toRack, last, toIndex);
    fixStereoVersions (toRack);
    select (toRack, juce::jlimit (0, last, toIndex));
}

void MainComponent::showSlotMenu (int rack, int slot, juce::Rectangle<int> screenArea)
{
    auto* plugin = engine.getPlugin (rack, slot);
    if (plugin == nullptr)
        return;

    const int numSlots = engine.getNumSlots (rack);
    const int slotId = engine.getSlotId (rack, slot);
    const bool hot = engine.isHot (slotId);

    juce::PopupMenu m;
    m.addSectionHeader (plugin->getName());
    m.addItem (1, "Bypass", true, engine.isSlotBypassed (rack, slot));
    m.addItem (2, "Open in its own window");
    m.addItem (8, hot ? "Unpin from Hot Plugins" : "Pin to Hot Plugins",
               hot || (int) engine.getHotPlugins().size() < RackEngine::maxHotPlugins);
    m.addItem (9, "Recalled by snapshots", true, engine.isSlotInScope (rack, slot));
    if (engine.getRackConfig (rack).isMonoToStereo())
        m.addItem (10, "Make this the stereo point", true, engine.getStereoPoint (rack) == slot);
    m.addItem (3, "Move up", slot > 0);
    m.addItem (4, "Move down", slot < numSlots - 1);
    m.addSeparator();
    m.addItem (6, "Parameters, GR source and MIDI Learn...");

    const auto desc = engine.getSlotDescription (rack, slot);
    const auto key = PluginLibrary::familyKey (desc);
    const bool estimating = library.getShowEstimatedGR (key, library.categoryFor (desc));
    m.addItem (7, "Estimate GR when not reported", true, estimating);
    m.addSeparator();
    m.addItem (5, "Remove");

    auto options = screenArea.isEmpty() ? juce::PopupMenu::Options()
                                        : juce::PopupMenu::Options().withTargetScreenArea (screenArea);

    juce::Component::SafePointer<MainComponent> safe (this);
    m.showMenuAsync (options, [safe, rack, slot, slotId, hot] (int result)
    {
        if (safe == nullptr)
            return;

        auto& e = safe->engine;
        switch (result)
        {
            case 1: e.setSlotBypassed (rack, slot, ! e.isSlotBypassed (rack, slot)); break;
            case 2: safe->popOutEditor (rack, slot); break;
            case 3: e.moveSlot (rack, slot, slot - 1); safe->select (rack, slot - 1); break;
            case 4: e.moveSlot (rack, slot, slot + 1); safe->select (rack, slot + 1); break;
            case 5: safe->removeSlot (rack, slot); break;
            case 6: safe->showParameters (rack, slot); break;
            case 8: if (hot) e.unpinHot (slotId); else e.pinHot (slotId); break;
            case 9: e.setSlotInScope (rack, slot, ! e.isSlotInScope (rack, slot)); break;
            case 10:
                safe->dock.releaseEditor();
                e.setStereoPoint (rack, slot);
                safe->fixStereoVersions (rack);
                safe->refreshAll();
                break;
            case 7:
                if (e.getPlugin (rack, slot) != nullptr)
                {
                    const auto d = e.getSlotDescription (rack, slot);
                    const auto k = PluginLibrary::familyKey (d);
                    safe->library.setShowEstimatedGR (k, ! safe->library.getShowEstimatedGR (k, safe->library.categoryFor (d)));
                    safe->strips.forgetMeters();
                }
                break;
            default: break;
        }
    });
}

void MainComponent::showParameters (int rack, int slot)
{
    auto* plugin = engine.getPlugin (rack, slot);
    if (plugin == nullptr)
        return;

    for (auto& w : inspectorWindows)
        if (w->plugin == plugin)
        {
            w->toFront (true);
            return;
        }

    juce::Component::SafePointer<MainComponent> safe (this);
    auto* inspector = new ParameterInspector (*plugin, engine.getSlotDescription (rack, slot), library, [safe]
    {
        if (safe != nullptr)
            safe->strips.forgetMeters(); // re-check GR sources
    });

    const int slotId = engine.getSlotId (rack, slot);
    inspector->onMidiLearn = [safe, slotId] (int param, bool follow)
    {
        int r, s;
        if (safe != nullptr && safe->engine.findSlot (slotId, r, s))
            safe->midi->learnParameter (r, s, param, follow);
    };

    const auto title = "Parameters: " + engine.getRackConfig (rack).name + " / " + plugin->getName();

    inspectorWindows.push_back (std::make_unique<InspectorWindow> (title, inspector, [safe, plugin]
    {
        juce::MessageManager::callAsync ([safe, plugin]
        {
            if (safe == nullptr)
                return;

            auto& v = safe->inspectorWindows;
            v.erase (std::remove_if (v.begin(), v.end(), [plugin] (const auto& w) { return w->plugin == plugin; }), v.end());
        });
    }));
}

void MainComponent::handleSlotDrop (int fromRack, int fromSlot, int toRack, int toIndex, bool copy)
{
    if (engine.getPlugin (fromRack, fromSlot) == nullptr || ! juce::isPositiveAndBelow (toRack, engine.getNumRacks()))
        return;

    // Reorder within a rack: no reload, settings untouched
    if (fromRack == toRack && ! copy)
    {
        const int target = toIndex > fromSlot ? toIndex - 1 : toIndex;
        engine.moveSlotTo (fromRack, fromSlot, target);
        fixStereoVersions (fromRack);
        select (toRack, juce::jlimit (0, engine.getNumSlots (toRack) - 1, target));
        return;
    }

    // Copy (with settings) into the target rack, then remove the original for a move
    const int movedId = engine.getSlotId (fromRack, fromSlot);
    const bool wasHot = engine.isHot (movedId);

    juce::String error;
    if (! engine.copyPlugin (fromRack, fromSlot, toRack, toIndex, error))
    {
        showError ("Couldn't copy plugin", error);
        return;
    }

    const int newIndex = juce::jlimit (0, engine.getNumSlots (toRack) - 1, toIndex);

    if (! copy)
    {
        // A moved plugin keeps its Hot Plugins pin
        if (wasHot)
        {
            auto list = engine.getHotPlugins();
            std::replace (list.begin(), list.end(), movedId, engine.getSlotId (toRack, newIndex));
            engine.setHotPlugins (list);
        }

        dock.releaseEditor();
        closeEditorFor (engine.getPlugin (fromRack, fromSlot));
        engine.removePlugin (fromRack, fromSlot);
        fixStereoVersions (fromRack);
    }

    fixStereoVersions (toRack);
    select (toRack, newIndex);
}

void MainComponent::popOutEditor (int rack, int slot)
{
    auto* plugin = engine.getPlugin (rack, slot);
    if (plugin == nullptr)
        return;

    for (auto& w : editorWindows)
        if (&w->processor == plugin)
        {
            w->toFront (true);
            return;
        }

    if (dock.getShownProcessor() == plugin)
        dock.releaseEditor(); // a plugin has only one editor at a time

    const auto title = engine.getRackConfig (rack).name + ": " + plugin->getName();
    juce::Component::SafePointer<MainComponent> safe (this);

    editorWindows.push_back (std::make_unique<PluginWindow> (*plugin, title, [safe, plugin]
    {
        juce::MessageManager::callAsync ([safe, plugin]
        {
            if (safe == nullptr)
                return;

            safe->closeEditorFor (plugin);
            safe->refreshAll(); // dock the editor again if it's selected
        });
    }));

    refreshAll();
}

void MainComponent::removeSlot (int rack, int slot)
{
    auto* plugin = engine.getPlugin (rack, slot);
    if (plugin == nullptr)
        return;

    dock.releaseEditor();
    closeEditorFor (plugin);
    engine.removePlugin (rack, slot);
    select (rack, juce::jmax (0, slot - 1));
}

//==============================================================================
// One or two windows
void MainComponent::setTwoWindowMode (bool shouldBeTwo)
{
    if (shouldBeTwo == isTwoWindowMode())
        return;

    // Plugin editors are native views: recreate them in their new window
    // rather than moving them between windows.
    dock.releaseEditor();

    if (shouldBeTwo)
    {
        removeChildComponent (&dock);

        juce::Component::SafePointer<MainComponent> safe (this);
        rackWindow = std::make_unique<RackViewWindow> ([safe]
        {
            if (safe != nullptr)
                juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->setTwoWindowMode (false); });
        });

        rackWindow->setContentNonOwned (&dock, false);
        rackWindow->addKeyListener (this);

        const auto saved = AppSettings::get ("rackWindow");

        if (saved.isEmpty() || ! rackWindow->restoreWindowStateFromString (saved))
        {
            // First time: use another screen if there is one
            const auto& displays = juce::Desktop::getInstance().getDisplays();
            const auto mainCentre = getScreenBounds().getCentre();
            const juce::Displays::Display* target = nullptr;

            for (auto& d : displays.displays)
                if (! d.totalArea.contains (mainCentre))
                {
                    target = &d;
                    break;
                }

            if (target != nullptr)
                rackWindow->setBounds (target->userArea.reduced (40));
            else
                rackWindow->centreWithSize (1100, 760);
        }

        rackWindow->setVisible (true);
        windowModeButton.setButtonText ("One Window");
    }
    else
    {
        AppSettings::set ("rackWindow", rackWindow->getWindowStateAsString());
        rackWindow->removeKeyListener (this);
        rackWindow->clearContentComponent();
        rackWindow.reset();
        addAndMakeVisible (dock);
        windowModeButton.setButtonText ("Two Windows");
    }

    windowModeButton.setToggleState (shouldBeTwo, juce::dontSendNotification);
    AppSettings::set ("twoWindows", shouldBeTwo ? "1" : "0");
    resized();
    refreshAll(); // shows the selected plugin again in its new home
}

//==============================================================================
// Floating editor windows
void MainComponent::closeEditorFor (juce::AudioProcessor* p)
{
    editorWindows.erase (std::remove_if (editorWindows.begin(), editorWindows.end(),
                                         [p] (const auto& w) { return &w->processor == p; }),
                         editorWindows.end());

    inspectorWindows.erase (std::remove_if (inspectorWindows.begin(), inspectorWindows.end(),
                                            [p] (const auto& w) { return w->plugin == p; }),
                            inspectorWindows.end());
}

void MainComponent::closeEditorsForRack (int rack)
{
    for (int s = 0; s < engine.getNumSlots (rack); ++s)
        closeEditorFor (engine.getPlugin (rack, s));
}

void MainComponent::closeAllEditors()
{
    dock.releaseEditor();
    editorWindows.clear();
    inspectorWindows.clear();
}

//==============================================================================
// Windows and files
void MainComponent::showDevices()
{
    if (devicesWindow != nullptr)
    {
        devicesWindow->toFront (true);
        return;
    }

    juce::Component::SafePointer<MainComponent> safe (this);

    DevicesPanel::Host host { engine, consoles, *midi, osc, webRemote,
        [safe] (int d) { if (safe != nullptr) safe->showDriverSettings (d); },
        [safe] (int c) { if (safe != nullptr) safe->showConsoleSettings (c); },
        [safe] { if (safe != nullptr) safe->addConsole(); },
        [safe] (int c) { if (safe != nullptr) safe->removeConsole (c); },
        [safe] { if (safe != nullptr) safe->saveConsoles(); },
        [safe] (int console, int channel) -> juce::String
        {
            if (safe == nullptr) return {};
            for (int r = 0; r < safe->engine.getNumRacks(); ++r)
            {
                const auto c = safe->engine.getRackConfig (r);
                if (c.consoleChannel == channel && (safe->consoles.size() < 2 || c.console == console))
                    return c.name;
            }
            return {};
        },
        [safe] { if (safe != nullptr) safe->showShortcuts(); },
        [safe] { return safe != nullptr ? (int) safe->extraShortcuts.size() + [&]{ int n = 0; for (int i = 0; i < UserKeysBar::numKeys; ++i) if (safe->keysBar.getShortcut (i).isValid()) ++n; return n; }() : 0; } };

    devicesPanel = new DevicesPanel (host);
    devicesWindow = std::make_unique<ToolWindow> ("LiveRack - Devices", devicesPanel, "devicesWindow", [safe]
    {
        juce::MessageManager::callAsync ([safe]
        {
            if (safe == nullptr) return;
            safe->devicesPanel = nullptr;
            safe->devicesWindow.reset();
        });
    });
}

void MainComponent::showDriverSettings (int driver)
{
    auto* selector = new juce::AudioDeviceSelectorComponent (engine.getDeviceManager (driver),
                                                             0, 128, 0, 128,
                                                             false, false, false, false);
    selector->setSize (560, 520);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = "Audio driver " + juce::String (driver + 1);
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void MainComponent::showRouting (int rack)
{
    if (routingWindow == nullptr)
    {
        juce::Component::SafePointer<MainComponent> safe (this);
        routingMatrix = new RoutingMatrix (engine);
        routingMatrix->slotLabelFor = [safe] (int d, int ch) { return safe != nullptr ? safe->slotLabelFor (d, ch) : juce::String(); };
        routingMatrix->onSelectRack = [safe] (int r)
        {
            if (safe != nullptr && r != safe->selectedRack)
            {
                safe->selectedRack = r;
                safe->selectedSlot = 0;
                safe->refreshAll();
            }
        };

        routingWindow = std::make_unique<ToolWindow> ("LiveRack - Routing", routingMatrix, "routingWindow", [safe]
        {
            juce::MessageManager::callAsync ([safe]
            {
                if (safe == nullptr) return;
                safe->routingMatrix = nullptr;
                safe->routingWindow.reset();
            });
        });
    }

    routingWindow->toFront (true);
    if (rack >= 0)
        routingMatrix->setSelectedRack (rack);
}

void MainComponent::showScope()
{
    if (scopeWindow != nullptr)
    {
        scopeWindow->toFront (true);
        return;
    }

    juce::Component::SafePointer<MainComponent> safe (this);
    scopeEditor = new ScopeEditor (engine);
    scopeWindow = std::make_unique<ToolWindow> ("LiveRack - Snapshot Scope", scopeEditor, "scopeWindow", [safe]
    {
        juce::MessageManager::callAsync ([safe]
        {
            if (safe == nullptr) return;
            safe->scopeEditor = nullptr;
            safe->scopeWindow.reset();
        });
    });
}

void MainComponent::showPluginScanner()
{
    auto* list = new juce::PluginListComponent (engine.getFormatManager(),
                                                engine.getKnownPlugins(),
                                                engine.getDeadMansPedalFile(),
                                                nullptr, false);
    list->setNumberOfThreadsForScanning (1); // keep the UI responsive while scanning
    list->setSize (760, 520);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (list);
    o.dialogTitle = "Plugins";
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

void MainComponent::saveSnapshot()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Save snapshot",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.liverack");

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File())
                return;

            if (auto xml = engine.createSnapshot())
                xml->writeTo (f.withFileExtension ("liverack"));
        });
}

void MainComponent::loadSnapshot()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load snapshot",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.liverack");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f.existsAsFile())
                loadSnapshotFile (f);
        });
}

void MainComponent::loadSnapshotFile (const juce::File& f)
{
    auto xml = juce::parseXML (f);

    if (xml == nullptr)
    {
        showError ("Couldn't read snapshot", f.getFullPathName());
        return;
    }

    // Same racks and plugins: recalled in place, editors stay open.
    // Otherwise everything is rebuilt, so editors must close first.
    const bool inPlace = engine.snapshotMatchesCurrentLayout (*xml);

    if (! inPlace)
        closeAllEditors();

    juce::String error;
    if (! engine.loadSnapshot (*xml, error))
        showError ("Snapshot loaded with problems", error);

    if (! inPlace)
        select (0, 0);
}

void MainComponent::showError (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, message);
}

//==============================================================================
// Consoles
juce::File MainComponent::consoleSettingsFile (int i) const
{
    return RackEngine::getSettingsDir().getChildFile (i == 0 ? juce::String ("console-link.xml")
                                                             : "console-link-" + juce::String (i + 1) + ".xml");
}

ConsoleLink* MainComponent::primaryConsole() const
{
    if (consoles.empty())
        return nullptr;
    return consoles[(size_t) juce::jlimit (0, (int) consoles.size() - 1, activeConsole)].get();
}

void MainComponent::wireConsole (int i)
{
    auto* c = consoles[(size_t) i].get();
    juce::Component::SafePointer<MainComponent> safe (this);

    c->onSceneRecalled = [safe, c] (int scene)
    {
        if (safe == nullptr) return;
        for (int k = 0; k < (int) safe->consoles.size(); ++k)
            if (safe->consoles[(size_t) k].get() == c)
                safe->activeConsole = k;
        if (c->getSettings().recallScenes)
            safe->recallScene (scene);
    };

    c->onChannelSelected = [safe, c] (int channel)
    {
        if (safe == nullptr || ! c->getSettings().followSelection) return;
        for (int k = 0; k < (int) safe->consoles.size(); ++k)
            if (safe->consoles[(size_t) k].get() == c)
                safe->selectRackForChannel (k, channel);
    };
}

void MainComponent::addConsole()
{
    consoles.push_back (std::make_unique<ConsoleLink>());
    wireConsole ((int) consoles.size() - 1);
    saveConsoles();
    if (devicesPanel != nullptr) devicesPanel->refresh();
    showConsoleSettings ((int) consoles.size() - 1);
}

void MainComponent::removeConsole (int i)
{
    if (! juce::isPositiveAndBelow (i, (int) consoles.size()))
        return;

    consoles.erase (consoles.begin() + i);
    if (consoles.empty())
    {
        consoles.push_back (std::make_unique<ConsoleLink>());
        wireConsole (0);
    }

    // Re-wire so callbacks know their new index
    for (int k = 0; k < (int) consoles.size(); ++k)
        wireConsole (k);

    activeConsole = 0;
    consoleSettingsFile ((int) consoles.size()).deleteFile();
    saveConsoles();
    if (devicesPanel != nullptr) devicesPanel->refresh();
}

void MainComponent::saveConsoles()
{
    for (int i = 0; i < (int) consoles.size(); ++i)
        consoles[(size_t) i]->saveSettingsTo (consoleSettingsFile (i));
    AppSettings::set ("consoles", juce::String ((int) consoles.size()));
}

void MainComponent::showConsoleSettings (int i)
{
    if (! juce::isPositiveAndBelow (i, (int) consoles.size()))
        return;

    juce::Component::SafePointer<MainComponent> safe (this);
    auto* panel = new ConsoleLinkPanel (*consoles[(size_t) i], [safe]
    {
        if (safe != nullptr)
            safe->saveConsoles();
    });

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (panel);
    o.dialogTitle = "Console " + juce::String (i + 1) + " settings";
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

juce::String MainComponent::slotLabelFor (int driver, int channel) const
{
    for (auto& c : consoles)
        for (auto& s : c->getSettings().slots)
            if (s.card != 0 && s.driver == driver && channel >= s.firstChannel && channel < s.firstChannel + s.numChannels)
                return (consoles.size() > 1 ? c->getDisplayName() + " " : juce::String()) + s.slot;
    return {};
}

void MainComponent::showStoreSceneDialog()
{
    auto* console = primaryConsole();
    if (console == nullptr || ! console->getSettings().sceneFolder.isDirectory())
    {
        showError ("No scene folder", "Choose a scene folder in Devices > console Settings first.");
        return;
    }

    sceneDialog = std::make_unique<juce::AlertWindow> ("Store Scene",
        "Saves the current racks and plugin settings (with the current scope) for a console scene number. "
        "When the console recalls that scene, LiveRack recalls this.",
        juce::MessageBoxIconType::NoIcon);

    sceneDialog->addTextEditor ("scene", juce::String (juce::jmax (0, console->getCurrentScene() >= 0 ? console->getCurrentScene() : currentScene)), "Scene number");
    sceneDialog->addButton ("Store", 1, juce::KeyPress (juce::KeyPress::returnKey));
    sceneDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MainComponent> safe (this);
    sceneDialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe] (int result)
    {
        if (safe != nullptr)
            safe->finishStoreScene (result);
    }), false);
}

void MainComponent::finishStoreScene (int result)
{
    if (sceneDialog != nullptr && result == 1)
        storeScene (juce::jmax (0, sceneDialog->getTextEditorContents ("scene").getIntValue()));

    juce::Component::SafePointer<MainComponent> safe (this);
    juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->sceneDialog.reset(); });
}

void MainComponent::storeScene (int scene)
{
    auto* console = primaryConsole();
    if (console == nullptr || ! console->getSettings().sceneFolder.isDirectory())
    {
        consoleNote = "Can't store: no scene folder set";
        return;
    }

    const auto file = console->newFileForScene (scene);
    auto xml = engine.createSnapshot();

    if (xml != nullptr && xml->writeTo (file))
    {
        consoleNote = "Stored scene " + juce::String (scene) + " as " + file.getFileName();
        currentScene = scene;
    }
    else
    {
        showError ("Couldn't store scene", file.getFullPathName());
    }
}

void MainComponent::recallScene (int scene)
{
    auto* console = primaryConsole();
    if (console == nullptr)
        return;

    currentScene = scene;
    const auto file = console->fileForScene (scene);

    if (! file.existsAsFile())
    {
        consoleNote = "Scene " + juce::String (scene) + ": no snapshot stored, racks unchanged";
        repaint();
        return;
    }

    loadSnapshotFile (file);
    consoleNote = "Scene " + juce::String (scene) + ": recalled " + file.getFileName();
    repaint();
}

// Next / previous stored scene in the scene folder
void MainComponent::stepScene (int direction)
{
    auto* console = primaryConsole();
    if (console == nullptr || ! console->getSettings().sceneFolder.isDirectory())
        return;

    juce::Array<int> numbers;
    for (auto& f : console->getSettings().sceneFolder.findChildFiles (juce::File::findFiles, false, "*.liverack"))
    {
        const auto name = f.getFileNameWithoutExtension();
        const auto digits = name.initialSectionContainingOnly ("0123456789");
        if (digits.isNotEmpty())
            numbers.addIfNotAlreadyThere (digits.getIntValue());
    }
    numbers.sort();

    if (numbers.isEmpty())
        return;

    int target = direction > 0 ? numbers.getFirst() : numbers.getLast();
    for (int i = 0; i < numbers.size(); ++i)
    {
        if (direction > 0 && numbers[i] > currentScene) { target = numbers[i]; break; }
        if (direction < 0 && numbers[numbers.size() - 1 - i] < currentScene) { target = numbers[numbers.size() - 1 - i]; break; }
    }

    recallScene (target);
}

void MainComponent::selectRackForChannel (int console, int channel)
{
    for (int i = 0; i < engine.getNumRacks(); ++i)
    {
        const auto c = engine.getRackConfig (i);
        if (c.consoleChannel == channel && (consoles.size() < 2 || c.console == console))
        {
            select (i, -1);
            return;
        }
    }
}

//==============================================================================
// Pages
void MainComponent::addPage()
{
    auto* w = new juce::AlertWindow ("New page", "A page shows just the racks you put on it.", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", "Page " + juce::String ((int) engine.getPages().size() + 1), "Name");
    if (juce::isPositiveAndBelow (selectedRack, engine.getNumRacks()))
        w->addComboBox ("start", { "Start with " + engine.getRackConfig (selectedRack).name, "Start empty" }, "Racks");
    w->addButton ("Add", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MainComponent> safe (this);
    const int rack = selectedRack;
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, rack] (int r)
    {
        if (safe == nullptr || r != 1)
            return;

        auto pages = safe->engine.getPages();
        RackPage p;
        p.name = w->getTextEditorContents ("name").trim();
        if (p.name.isEmpty()) p.name = "Page";
        auto* start = w->getComboBoxComponent ("start");
        if (start != nullptr && start->getSelectedItemIndex() == 0)
            p.rackIds.push_back (safe->engine.getRackId (rack));
        pages.push_back (p);
        safe->engine.setPages (pages);
        safe->engine.setCurrentPage ((int) pages.size() - 1);
    }), true);
}

void MainComponent::showPageMenu (int page)
{
    if (! juce::isPositiveAndBelow (page, (int) engine.getPages().size()))
        return;

    const int count = (int) engine.getPages().size();
    juce::PopupMenu m;
    m.addSectionHeader (engine.getPages()[(size_t) page].name);
    m.addItem (1, "Rename...");
    if (juce::isPositiveAndBelow (selectedRack, engine.getNumRacks()))
        m.addItem (2, "Add " + engine.getRackConfig (selectedRack).name + " to this page", ! engine.isRackOnPage (selectedRack, page));
    m.addItem (3, "Move left", page > 0);
    m.addItem (4, "Move right", page < count - 1);
    m.addSeparator();
    m.addItem (5, "Delete page (racks stay)");

    juce::Component::SafePointer<MainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, page] (int r)
    {
        if (safe == nullptr) return;
        auto& e = safe->engine;
        auto pages = e.getPages();

        if (r == 1)
        {
            auto* w = new juce::AlertWindow ("Rename page", {}, juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", pages[(size_t) page].name);
            w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, page, w] (int res)
            {
                if (safe == nullptr || res != 1) return;
                auto ps = safe->engine.getPages();
                if (juce::isPositiveAndBelow (page, (int) ps.size()) && w->getTextEditorContents ("name").trim().isNotEmpty())
                    ps[(size_t) page].name = w->getTextEditorContents ("name").trim();
                safe->engine.setPages (ps);
            }), true);
        }
        if (r == 2) e.setRackOnPage (safe->selectedRack, page, true);
        if (r == 3 || r == 4)
        {
            const int other = r == 3 ? page - 1 : page + 1;
            std::swap (pages[(size_t) page], pages[(size_t) other]);
            e.setPages (pages);
            e.setCurrentPage (other);
        }
        if (r == 5)
        {
            pages.erase (pages.begin() + page);
            e.setPages (pages);
            e.setCurrentPage (-1);
        }
    });
}

void MainComponent::showRackOnSomePage (int rack)
{
    if (! juce::isPositiveAndBelow (rack, engine.getNumRacks()) || engine.isRackOnPage (rack, engine.getCurrentPage()))
        return;

    for (int p = 0; p < (int) engine.getPages().size(); ++p)
        if (engine.isRackOnPage (rack, p))
        {
            engine.setCurrentPage (p);
            return;
        }

    engine.setCurrentPage (-1); // only on All
}

//==============================================================================
// Mono-to-stereo racks
PluginLibrary::Want MainComponent::wantForSlot (int rack, int insertIndex, const PluginLibrary::Family& f) const
{
    const auto cfg = engine.getRackConfig (rack);
    if (cfg.stereo)              return PluginLibrary::Want::stereo;
    if (! cfg.isMonoToStereo())  return PluginLibrary::Want::mono;

    const int point = engine.getStereoPoint (rack);
    if (point >= 0)
        return insertIndex > point ? PluginLibrary::Want::stereo : PluginLibrary::Want::mono;

    return PluginLibrary::suitsStereoPoint (f.category) ? PluginLibrary::Want::monoToStereo : PluginLibrary::Want::mono;
}

// After plugins move in a mono-to-stereo rack, swap fixed versions (e.g.
// Waves Mono / Mono-Stereo / Stereo) so the chain is mono, then the stereo
// point, then stereo. Settings are carried over.
void MainComponent::fixStereoVersions (int rack)
{
    if (! engine.getRackConfig (rack).isMonoToStereo())
        return;

    const int point = engine.getStereoPoint (rack);

    for (int s = 0; s < engine.getNumSlots (rack); ++s)
    {
        const auto want = point < 0 || s < point ? PluginLibrary::Want::mono
                        : s == point ? PluginLibrary::Want::monoToStereo
                                     : PluginLibrary::Want::stereo;

        const auto desc = engine.getSlotDescription (rack, s);
        const auto label = PluginLibrary::versionLabel (desc).toLowerCase();
        if (label.isEmpty())
            continue; // a flexible plugin: its layout follows the chain already

        const juce::String wanted = want == PluginLibrary::Want::mono ? "mono" : want == PluginLibrary::Want::stereo ? "stereo" : "mono/stereo";
        if (label == wanted)
            continue;

        auto* f = library.findFamily (PluginLibrary::familyKey (desc));
        if (f == nullptr || ! PluginLibrary::hasVersion (*f, want))
            continue;

        dock.releaseEditor();
        closeEditorFor (engine.getPlugin (rack, s));

        juce::String error;
        if (! engine.replacePlugin (rack, s, PluginLibrary::bestVersionFor (*f, want), error))
            consoleNote = "Couldn't switch " + desc.name + ": " + error;
    }
}

//==============================================================================
// Keyboard shortcuts
juce::String MainComponent::shortcutOwner (const juce::KeyPress& k) const
{
    if (const int key = keysBar.keyForShortcut (k); key >= 0)
        return "User Key " + juce::String (key + 1);

    for (auto& sc : extraShortcuts)
        if (sc.key == k)
            return sc.action.describe (engine);

    return {};
}

void MainComponent::clearShortcut (const juce::KeyPress& k)
{
    if (const int key = keysBar.keyForShortcut (k); key >= 0)
        keysBar.setShortcut (key, {});

    extraShortcuts.erase (std::remove_if (extraShortcuts.begin(), extraShortcuts.end(),
                                          [&k] (const Shortcut& sc) { return sc.key == k; }),
                          extraShortcuts.end());
    Shortcuts::saveExtras (extraShortcuts);
}

void MainComponent::captureShortcut (const juce::String& forWhat, std::function<void (juce::KeyPress)> onAccepted)
{
    juce::Component::SafePointer<MainComponent> safe (this);

    KeyCapture::launch (forWhat, [safe, forWhat, onAccepted] (juce::KeyPress k)
    {
        if (safe == nullptr)
            return;

        if (Shortcuts::isReserved (k))
        {
            safe->showError ("Can't use " + Shortcuts::describe (k), "macOS or text editing uses that one. Pick another.");
            return;
        }

        const auto owner = safe->shortcutOwner (k);
        if (owner.isEmpty())
        {
            onAccepted (k);
            if (safe->shortcutsEditor != nullptr) safe->shortcutsEditor->refresh();
            return;
        }

        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon,
            Shortcuts::describe (k) + " is in use",
            "It's the shortcut for " + owner + ". Use it for " + forWhat + " instead?",
            "Use it here", "Cancel", nullptr,
            juce::ModalCallbackFunction::create ([safe, k, onAccepted] (int r)
            {
                if (safe == nullptr || r != 1) return;
                safe->clearShortcut (k);
                onAccepted (k);
                if (safe->shortcutsEditor != nullptr) safe->shortcutsEditor->refresh();
            }));
    });
}

void MainComponent::showShortcuts()
{
    if (shortcutsWindow != nullptr)
    {
        shortcutsWindow->toFront (true);
        return;
    }

    juce::Component::SafePointer<MainComponent> safe (this);
    ShortcutsEditor::Host host {
        [] { return UserKeysBar::numKeys; },
        [safe] (int i) { return safe != nullptr ? safe->keysBar.getShortcut (i) : juce::KeyPress(); },
        [safe] (int i, juce::KeyPress k) { if (safe != nullptr) safe->keysBar.setShortcut (i, k); },
        [safe] (int i) { return safe != nullptr ? safe->keysBar.getKey (i).describe (safe->engine) : juce::String(); },
        [safe]() -> std::vector<Shortcut>& { return safe->extraShortcuts; },
        [safe] { if (safe != nullptr) Shortcuts::saveExtras (safe->extraShortcuts); },
        [safe] (const Action& a) { return safe != nullptr ? a.describe (safe->engine) : juce::String(); },
        [safe] (std::function<void (Action)> cb) { if (safe != nullptr) Action::showChooser (safe->engine, {}, safe->selectedRack, cb); },
        [safe] (const juce::String& what, std::function<void (juce::KeyPress)> cb) { if (safe != nullptr) safe->captureShortcut (what, cb); }
    };

    shortcutsEditor = new ShortcutsEditor (host);
    shortcutsWindow = std::make_unique<ToolWindow> ("LiveRack - Keyboard Shortcuts", shortcutsEditor, "shortcutsWindow", [safe]
    {
        juce::MessageManager::callAsync ([safe]
        {
            if (safe == nullptr) return;
            safe->shortcutsEditor = nullptr;
            safe->shortcutsWindow.reset();
        });
    });
}

//==============================================================================
// iPad / web remote
juce::String MainComponent::buildWebState (const juce::String& query)
{
    auto obj = [] { return juce::var (new juce::DynamicObject()); };
    auto root = obj();
    auto* r = root.getDynamicObject();

    r->setProperty ("scene", currentScene);
    r->setProperty ("bpm", engine.getBpm());
    r->setProperty ("page", engine.getCurrentPage());
    r->setProperty ("selected", selectedRack);
    r->setProperty ("selectedSlot", selectedSlot);

    juce::Array<juce::var> pages;
    for (auto& p : engine.getPages())
        pages.add (p.name);
    r->setProperty ("pages", pages);

    const auto visible = engine.getVisibleRacks();
    juce::Array<juce::var> racks;

    for (int i = 0; i < engine.getNumRacks(); ++i)
    {
        const auto cfg = engine.getRackConfig (i);
        auto rack = obj();
        auto* ro = rack.getDynamicObject();
        ro->setProperty ("name", cfg.name);
        ro->setProperty ("colour", "#" + RackStripsView::colourFor (engine, i).toDisplayString (false));
        ro->setProperty ("bypass", engine.isRackBypassed (i));
        ro->setProperty ("safe", cfg.recallSafe);
        ro->setProperty ("format", cfg.formatName());
        ro->setProperty ("group", RackConfig::groupName (cfg.latencyGroup));
        ro->setProperty ("gr", strips.getMaxGainReduction (i));
        ro->setProperty ("level", strips.getOutputLevel (i));
        ro->setProperty ("visible", std::find (visible.begin(), visible.end(), i) != visible.end());

        const int point = engine.getStereoPoint (i);
        juce::Array<juce::var> slots;
        for (int s = 0; s < engine.getNumSlots (i); ++s)
        {
            auto slot = obj();
            auto* so = slot.getDynamicObject();
            auto* p = engine.getPlugin (i, s);
            so->setProperty ("name", p != nullptr ? p->getName() : juce::String ("?"));
            so->setProperty ("bypass", engine.isSlotBypassed (i, s));
            so->setProperty ("sp", s == point);
            slots.add (slot);
        }
        ro->setProperty ("slots", slots);
        racks.add (rack);
    }
    r->setProperty ("racks", racks);

    juce::Array<juce::var> keys;
    for (int k = 0; k < UserKeysBar::numKeys; ++k)
    {
        auto key = obj();
        const auto& a = keysBar.getKey (k);
        key.getDynamicObject()->setProperty ("label", a.type == Action::none ? juce::String() : a.describe (engine));
        key.getDynamicObject()->setProperty ("lit", a.type != Action::none && isActionActive (a));
        keys.add (key);
    }
    r->setProperty ("keys", keys);

    // The plugin the remote has open: every parameter
    const auto viewText = query.fromFirstOccurrenceOf ("view=", false, false).upToFirstOccurrenceOf ("&", false, false);
    const int vr = viewText.upToFirstOccurrenceOf (":", false, false).getIntValue();
    const int vs = viewText.fromFirstOccurrenceOf (":", false, false).getIntValue();

    if (viewText.isNotEmpty())
        if (auto* plugin = engine.getPlugin (vr, vs))
        {
            auto view = obj();
            auto* vo = view.getDynamicObject();
            vo->setProperty ("rack", vr);
            vo->setProperty ("slot", vs);
            vo->setProperty ("rackName", engine.getRackConfig (vr).name);
            vo->setProperty ("name", plugin->getName());
            vo->setProperty ("bypass", engine.isSlotBypassed (vr, vs));

            juce::Array<juce::var> params;
            const auto& list = plugin->getParameters();
            const int meterIndex = GainReduction::findMeterParameter (*plugin);
            for (int i = 0; i < juce::jmin (list.size(), 1000); ++i)
            {
                auto* param = list[i];
                const auto name = param->getName (60);
                if (name.isEmpty() || param == plugin->getBypassParameter())
                    continue; // bypass has its own button

                using Cat = juce::AudioProcessorParameter::Category;
                const auto cat = param->getCategory();
                const bool meter = cat == Cat::inputMeter || cat == Cat::outputMeter || cat == Cat::analysisMeter
                                || cat == Cat::compressorLimiterGainReductionMeter || cat == Cat::expanderGateGainReductionMeter
                                || cat == Cat::otherMeter || i == meterIndex;
                auto pv = obj();
                auto* po = pv.getDynamicObject();
                po->setProperty ("i", i);
                po->setProperty ("name", name);
                po->setProperty ("value", param->getValue());
                po->setProperty ("text", param->getCurrentValueAsText());
                po->setProperty ("label", param->getLabel());
                po->setProperty ("ro", meter); // meters are shown, not dragged
                params.add (pv);
            }
            vo->setProperty ("params", params);
            r->setProperty ("view", view);
        }

    return juce::JSON::toString (root, true);
}

void MainComponent::handleWebCommand (const juce::var& c)
{
    const auto cmd = c["cmd"].toString();
    const int rack = c.hasProperty ("rack") ? (int) c["rack"] : -1;
    const int slot = c.hasProperty ("slot") ? (int) c["slot"] : -1;
    const int value = c.hasProperty ("value") && ! c["value"].isDouble() ? (int) c["value"] : -1;

    auto make = [] (Action::Type t, int r = -1, int id = 0, int n = 0)
    {
        Action a; a.type = t; a.rack = r; a.slotId = id; a.number = n; return a;
    };

    if (cmd == "rackBypass")
        performAction (make (Action::rackBypass, rack), value);
    else if (cmd == "select" && juce::isPositiveAndBelow (rack, engine.getNumRacks()))
        select (rack, slot);
    else if (cmd == "slotBypass" && engine.getPlugin (rack, slot) != nullptr)
        engine.setSlotBypassed (rack, slot, value < 0 ? ! engine.isSlotBypassed (rack, slot) : value > 0);
    else if (cmd == "param")
    {
        if (auto* plugin = engine.getPlugin (rack, slot))
            if (auto* param = plugin->getParameters()[(int) c["index"]])
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (float) (double) c["value"]));
                param->endChangeGesture();
            }
    }
    else if (cmd == "key")
        keysBar.press ((int) c["index"]);
    else if (cmd == "page")
        engine.setCurrentPage ((int) c["page"]);
    else if (cmd == "tap")
        engine.tapTempo();
    else if (cmd == "sceneStep")
        stepScene ((int) c["dir"]);
    else if (cmd == "scene")
        recallScene ((int) c["number"]);
}
