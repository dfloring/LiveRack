#include "MainComponent.h"
#include "IsolatedPlugin.h"
#include "ServerMode.h"
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
        : DocumentWindow ("LiveRack - Rack Window", Theme::background, DocumentWindow::allButtons),
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
// A setup screen (Devices, Routing, Plugins, Scope, Shortcuts). It shows in
// the main window in place of the racks, which suits a single screen, or pops
// out into its own window. Where each one goes is remembered.
class MainComponent::ToolWindow
{
public:
    ToolWindow (MainComponent& o, const juce::String& t, juce::Component* c, const juce::String& key, std::function<void()> onClose)
        : owner (o), title (t), settingsKey (key), closeCallback (std::move (onClose)), frame (*this)
    {
        content.reset (c);
        frame.addAndMakeVisible (*content);
        windowSize = { juce::jmax (480, content->getWidth()), content->getHeight() + Frame::headerHeight };

        if (AppSettings::get (settingsKey + "Popped") == "1")
            openWindow();
        else
            owner.attachTool (this);
        frame.update();
    }

    ~ToolWindow()
    {
        closeWindow();
        owner.detachTool (this);
        frame.removeChildComponent (content.get());
    }

    juce::Component& getFrame() { return frame; }
    bool isPoppedOut() const { return window != nullptr; }
    void close() { if (closeCallback != nullptr) closeCallback(); }

    void toFront (bool)
    {
        if (window != nullptr)
            window->toFront (true);
        else
            owner.attachTool (this);
    }

    void setPoppedOut (bool shouldPopOut)
    {
        if (shouldPopOut == isPoppedOut())
            return;

        AppSettings::set (settingsKey + "Popped", shouldPopOut ? "1" : "0");

        if (shouldPopOut)
        {
            owner.detachTool (this);
            openWindow();
        }
        else
        {
            closeWindow();
            owner.attachTool (this);
        }
    }

private:
    class Frame : public juce::Component
    {
    public:
        static constexpr int headerHeight = 44;

        explicit Frame (ToolWindow& t) : tool (t)
        {
            addAndMakeVisible (moveButton);
            addAndMakeVisible (closeButton);

            // Moving between windows deletes a window: never from inside its own click
            moveButton.onClick = [this]
            {
                juce::Component::SafePointer<Frame> safe (this);
                juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->tool.setPoppedOut (! safe->tool.isPoppedOut()); });
            };
            closeButton.onClick = [this] { tool.close(); };
            closeButton.setTooltip ("Back to the racks (Esc)");
        }

        void update()
        {
            const bool popped = tool.isPoppedOut();
            moveButton.setButtonText (popped ? "Show in Main Window" : "Pop Out");
            moveButton.setTooltip (popped ? "Show this in the main window, in place of the racks"
                                          : "Open this in its own window, for a second screen");
            closeButton.setVisible (! popped); // its window has its own close button
            resized();
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::background);
            if (tool.isPoppedOut())
                return;

            g.setColour (Theme::text);
            g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
            g.drawText (tool.title, getLocalBounds().removeFromTop (headerHeight).withTrimmedLeft (12),
                        juce::Justification::centredLeft);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            auto header = area.removeFromTop (headerHeight).reduced (8, 6);
            if (closeButton.isVisible())
            {
                closeButton.setBounds (header.removeFromRight (120));
                header.removeFromRight (6);
            }
            moveButton.setBounds (header.removeFromRight (tool.isPoppedOut() ? 170 : 96));

            if (auto* c = tool.content.get())
                c->setBounds (area);
        }

    private:
        ToolWindow& tool;
        juce::TextButton moveButton, closeButton { juce::String::fromUTF8 ("\u2039 Racks") };
    };

    class Window : public juce::DocumentWindow
    {
    public:
        Window (ToolWindow& t)
            : DocumentWindow ("LiveRack - " + t.title, Theme::background, DocumentWindow::closeButton | DocumentWindow::minimiseButton),
              tool (t)
        {
            setUsingNativeTitleBar (true);
            setContentNonOwned (&t.frame, true);
            setResizable (true, false);

            const auto saved = AppSettings::get (t.settingsKey);
            if (saved.isEmpty() || ! restoreWindowStateFromString (saved))
                centreWithSize (getWidth(), getHeight());

            setVisible (true);
        }

        void closeButtonPressed() override { tool.close(); }

    private:
        ToolWindow& tool;
    };

    void openWindow()
    {
        frame.setSize (windowSize.x, windowSize.y);
        window = std::make_unique<Window> (*this);
        frame.update();
    }

    void closeWindow()
    {
        if (window == nullptr)
            return;

        AppSettings::set (settingsKey, window->getWindowStateAsString());
        window->clearContentComponent();
        window.reset();
        frame.update();
    }

    MainComponent& owner;
    juce::String title, settingsKey;
    std::function<void()> closeCallback;
    Frame frame;
    std::unique_ptr<juce::Component> content;
    std::unique_ptr<Window> window;
    juce::Point<int> windowSize;
};

// BPM read-out: click to type a tempo
class MainComponent::BpmDisplay : public juce::Component,
                                  public juce::SettableTooltipClient
{
public:
    explicit BpmDisplay (RackEngine& e) : engine (e) { setTooltip ("Tempo for tempo-synced plugins. Click to type, TAP (or T) to tap."); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

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
        g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
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
        g.fillAll (juce::Colour (0xff0d0f11));
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
                chip (Theme::red, "Driver " + juce::String (d + 1),
                      e.getDriverWaitingFor (d).isNotEmpty() ? "waiting for " + e.getDriverWaitingFor (d)
                                                                    : juce::String ("no device: open Devices"), false);
                continue;
            }

            const bool recentDrop = s.dropouts > 0 && juce::Time::getCurrentTime() - s.lastDropout < juce::RelativeTime::minutes (5);
            juce::String rest;
            rest << juce::String (s.sampleRate / 1000.0, 1) << " kHz  " << s.bufferSize << " smp  DSP "
                 << juce::String ((int) std::ceil (s.cpu * 100.0)) << "%, longest block " << juce::String ((int) std::ceil (s.worstBlock * 100.0)) << "%";
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
    {
        juce::Component::SafePointer<MainComponent> safe (this);
        IsolatedPlugin::onEvent = [safe] (const juce::String& text)
        {
            if (safe == nullptr) return;
            safe->consoleNote = text;
            safe->repaint();
        };
    }

    bpmDisplay = std::make_unique<BpmDisplay> (engine);
    statusBar = std::make_unique<StatusBar> (*this);

    for (juce::Button* b : { (juce::Button*) &devicesButton, (juce::Button*) &routingButton, (juce::Button*) &scopeButton,
                             (juce::Button*) &scanButton, (juce::Button*) &storeSceneButton, (juce::Button*) &addRackButton,
                             (juce::Button*) &windowModeButton, (juce::Button*) &stackRowsButton, (juce::Button*) &saveButton,
                             (juce::Button*) &loadButton, (juce::Button*) &tapButton, (juce::Button*) &prevSceneButton,
                             (juce::Button*) &nextSceneButton })
        addAndMakeVisible (b);

    addAndMakeVisible (*bpmDisplay);
    addAndMakeVisible (hotBar);
    addAndMakeVisible (pageTabs);
    addAndMakeVisible (strips);
    addAndMakeVisible (dock);
    addAndMakeVisible (keysBar);
    addAndMakeVisible (*statusBar);

    saveButton.setLabel ("Save");
    loadButton.setLabel ("Show");
    devicesButton.setLabel ("Devices");
    routingButton.setLabel ("Routing");
    scanButton.setLabel ("Plugins");
    scopeButton.setLabel ("Scope");
    addRackButton.setLabel ("Add Rack");
    windowModeButton.setLabel ("Rack Window");
    stackRowsButton.setLabel ("Stack Rows");

    devicesButton.onClick    = [this] { toggleTool (devicesWindow, [this] { showDevices(); }); };
    routingButton.onClick    = [this] { toggleTool (routingWindow, [this] { showRouting (selectedRack); }); };
    scopeButton.onClick      = [this] { toggleTool (scopeWindow, [this] { showScope(); }); };
    scanButton.onClick       = [this] { toggleTool (pluginsWindow, [this] { showPluginScanner(); }); };
    storeSceneButton.onClick = [this] { showStoreSceneDialog(); };
    addRackButton.onClick    = [this] { addRack(); };
    windowModeButton.onClick = [this] { setTwoWindowMode (! isTwoWindowMode()); };
    tapButton.onClick        = [this] { engine.tapTempo(); };
    prevSceneButton.onClick  = [this] { stepScene (-1); };
    nextSceneButton.onClick  = [this] { stepScene (1); };
    tapButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
    tapButton.setColour (juce::TextButton::textColourOffId, Theme::background);
    storeSceneButton.setTooltip ("Store a snapshot of the plugin settings in the show");

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
    saveButton.onClick = [this] { saveShow (false); };
    loadButton.onClick = [this] { showShowMenu(); };

    strips.onSelect    = [this] (int r, int s) { select (r, s); };
    strips.onAddPlugin = [this] (int r) { showAddPluginMenu (r); };
    strips.onSlotMenu  = [this] (int r, int s) { showSlotMenu (r, s); };
    strips.onRackMenu  = [this] (int r) { showRackMenu (r); };
    strips.onEditRack  = [this] (int r) { showRackDialog (r); };
    strips.onOpenSlot  = [this] (int r, int s) { select (r, s); if (! isTwoWindowMode()) setFullView (true); };
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
    dock.onToggleFullView = [this] { setFullView (! fullView); };
    dock.setFullView (false, true);
    dock.levelFor = [this] (int r, bool out) { return strips.getLevel (r, out); };
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
    {
        juce::Component::SafePointer<MainComponent> safe (this);
        webRemote.openPluginView = [safe] (const juce::String& q, juce::String& error) -> std::shared_ptr<PluginViewCapture>
        {
            if (safe == nullptr) { error = "LiveRack is closing."; return nullptr; }
            return safe->openRemoteView (q, error);
        };
        webRemote.closePluginView = [safe] (std::shared_ptr<PluginViewCapture> c)
        {
            if (safe != nullptr)
                safe->closeRemoteView (std::move (c));
            else if (c != nullptr)
                c->stop();
        };
    }

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

    if (ServerMode::wasRestartedAfterCrash())
        consoleNote = "LiveRack quit unexpectedly and was restarted";

    keysBar.load(); // the show's own keys, if it has them

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
    IsolatedPlugin::onEvent = nullptr;
    stopTimer();
    removeKeyListener (this);
    engine.getKnownPlugins().removeChangeListener (this);
    engine.removeChangeListener (this);
    osc.onMessage = nullptr;
    webRemote.setEnabled (false, webRemote.getPort());
    webRemote.buildState = nullptr;
    webRemote.onCommand = nullptr;
    webRemote.openPluginView = nullptr;
    webRemote.closePluginView = nullptr;
    for (auto& v : remoteViews)
        if (auto c = v.capture.lock())
            c->stop();
    remoteViews.clear();

    for (auto& c : consoles)
    {
        c->onSceneRecalled = nullptr;
        c->onChannelSelected = nullptr;
    }
    saveConsoles(); // keeps anything learned
    keysBar.save();

    devicesWindow.reset();
    pluginsWindow.reset();
    shortcutsWindow.reset();
    routingWindow.reset();
    scopeWindow.reset();
    rackDialog.reset();
    sceneDialog.reset();
    pickerOverlay.reset();
    closeAllEditors(); // editors must die before their plugins
    midi.reset();

    storeWindowLayout(); // the engine keeps the session as it closes

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
    g.setColour (Theme::panel);
    g.fillRect (0, 0, getWidth(), toolbarHeight);
    g.setColour (Theme::border);
    g.fillRect (0, toolbarHeight, getWidth(), 1);

    auto logo = juce::Rectangle<float> (16.0f, (toolbarHeight - 28) / 2.0f, 28.0f, 28.0f);
    g.setColour (Theme::accent);
    g.fillRoundedRectangle (logo, 6.0f);
    g.setColour (Theme::background);
    for (int i = 0; i < 3; ++i)
        g.fillRoundedRectangle (logo.getX() + 7.0f, logo.getY() + 8.0f + i * 5.0f, 14.0f, 2.0f, 1.0f);

    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText ("LiveRack", 52, 0, 90, toolbarHeight, juce::Justification::centredLeft);

    // Button groups sit in sunken wells
    for (auto& grp : toolbarGroups)
    {
        g.setColour (Theme::well);
        g.fillRoundedRectangle (grp.toFloat(), 8.0f);
        g.setColour (Theme::border);
        g.drawRoundedRectangle (grp.toFloat().reduced (0.5f), 8.0f, 1.0f);
    }

    // Snapshot chip: console light, number, name (click for the list)
    auto chip = sceneChip.toFloat();
    auto* console = primaryConsole();
    const bool linked = console != nullptr && console->isConnected();
    Theme::drawLight (g, juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ chip.getX() + 10.0f, chip.getCentreY() }),
                      linked ? Theme::green : Theme::border, linked);

    auto t = chip.withTrimmedLeft (22.0f).reduced (0.0f, 3.0f);
    const auto* info = engine.getSnapshotInfo (currentScene);
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText (currentScene >= 0 ? "SNAPSHOT " + juce::String (currentScene) : juce::String ("SNAPSHOTS"),
                t.removeFromTop (t.getHeight() * 0.42f).toNearestInt(), juce::Justification::bottomLeft);
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    juce::String name = currentScene < 0 ? juce::String ((int) engine.getSnapshots().size()) + " in show"
                      : info == nullptr ? juce::String ("Not stored")
                      : info->name.isNotEmpty() ? info->name : juce::String ("Untitled");
    g.drawFittedText (name, t.toNearestInt(), juce::Justification::topLeft, 1, 0.8f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    auto bar = area.removeFromTop (toolbarHeight).reduced (12, 10);
    bar.removeFromLeft (140); // logo and name
    toolbarGroups.clear();

    // Labels under the icons when there's room for them
    Theme::IconButton* labelled[] = { &saveButton, &loadButton, &devicesButton, &routingButton, &scanButton,
                                      &scopeButton, &addRackButton, &windowModeButton, &stackRowsButton };
    int labelledWidth = 0;
    for (auto* b : labelled)
        labelledWidth += b->getLabelledWidth() - 40;
    const bool showLabels = bar.getWidth() >= 1010 + labelledWidth;
    for (auto* b : labelled)
        b->setShowsLabel (showLabels);
    auto w = [showLabels] (Theme::IconButton& b) { return showLabels ? b.getLabelledWidth() : 40; };

    // A group of buttons in a well, laid out left to right
    auto group = [this, &bar] (std::initializer_list<std::pair<juce::Component*, int>> items, bool fromRight = false)
    {
        int w = 4;
        for (auto& it : items) w += it.second + 4;
        auto grp = fromRight ? bar.removeFromRight (w) : bar.removeFromLeft (w);
        toolbarGroups.push_back (grp);
        auto inner = grp.reduced (4);
        for (auto& it : items)
        {
            auto cell = inner.removeFromLeft (it.second);
            if (it.first != nullptr)
                it.first->setBounds (cell);
            else
                sceneChip = cell;
            inner.removeFromLeft (4);
        }
        if (fromRight) bar.removeFromRight (12); else bar.removeFromLeft (12);
    };

    group ({ { &saveButton, w (saveButton) }, { &loadButton, w (loadButton) } });
    group ({ { &prevSceneButton, 34 }, { nullptr, 168 }, { &nextSceneButton, 34 }, { &storeSceneButton, 64 } });
    group ({ { &devicesButton, w (devicesButton) }, { &routingButton, w (routingButton) }, { &scanButton, w (scanButton) }, { &scopeButton, w (scopeButton) } });
    group ({ { &addRackButton, w (addRackButton) }, { &windowModeButton, w (windowModeButton) }, { &stackRowsButton, w (stackRowsButton) } });
    group ({ { bpmDisplay.get(), 84 }, { &tapButton, 60 } }, true);

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

    // A setup screen in front covers the racks (the User Keys stay)
    const bool toolInFront = ! tools.empty();
    hotBar.setVisible (! toolInFront);
    if (toolInFront)
    {
        tools.back()->getFrame().setBounds (area.withTop (hotBar.getY()));
        strips.setVisible (false);
        pageTabs.setVisible (false);
        dock.setVisible (false);
        return;
    }
    dock.setVisible (true);

    if (isTwoWindowMode())
    {
        strips.setVisible (true);
        pageTabs.setVisible (true);
        strips.setBounds (area); // the plugin panel lives in the rack view window
        return;
    }

    strips.setVisible (! fullView);
    pageTabs.setVisible (! fullView);

    if (fullView)
    {
        // The rack's plugins get the whole window, under the Hot Plugins row
        area.setTop (pageTabs.getY());
        dock.setBounds (area);
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
        if (source == &engine && juce::Time::getMillisecondCounter() > ignoreEditsUntil)
            engine.setShowEdited (true);
        refreshAll();
    }
}

void MainComponent::timerCallback()
{
    strips.updateMeters();
    dock.updateMeters();
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
        repaint (sceneChip.expanded (24)); // console light
        updateWindowTitle();
    }

    // Keep a copy of an edited show, in case of a crash
    if (ticks % (30 * 60) == 0 && engine.isShowEdited())
    {
        storeWindowLayout();
        engine.saveSession();
    }
}

bool MainComponent::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (key == juce::KeyPress ('s', juce::ModifierKeys::commandModifier, 0)) { saveShow (false); return true; }
    if (key == juce::KeyPress ('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) { saveShow (true); return true; }
    if (key == juce::KeyPress ('o', juce::ModifierKeys::commandModifier, 0)) { openShow(); return true; }
    if (key == juce::KeyPress::escapeKey && ! tools.empty()) { tools.back()->close(); return true; }
    if (key == juce::KeyPress::escapeKey && isFullView()) { setFullView (false); return true; }

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
        case Action::fullView:     setFullView (value < 0 ? ! isFullView() : value > 0); break;
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
        case Action::fullView:     return isFullView();
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
    else if (p[1] == "view" && p.size() >= 3 && p[2] == "fullview")
        performAction (make (Action::fullView), value);
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

    // While the plugin picker or a setup screen covers the panel, the docked
    // plugin stays closed (its native view would draw over them)
    if (pickerOverlay == nullptr && ! dockIsCovered())
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
    juce::String error;
    if (! insertPlugin (pluginId, toRack, toIndex, error) && error.isNotEmpty())
        showError ("Couldn't load plugin", error);
}

bool MainComponent::insertPlugin (const juce::String& pluginId, int toRack, int toIndex, juce::String& error)
{
    if (! juce::isPositiveAndBelow (toRack, engine.getNumRacks()))
        return false;

    juce::PluginDescription desc;

    if (pluginId.startsWith ("family:"))
    {
        auto* f = library.findFamily (pluginId.fromFirstOccurrenceOf ("family:", false, false));
        if (f == nullptr)
        {
            error = "That plugin isn't in the plugin list";
            return false;
        }
        desc = PluginLibrary::bestVersionFor (*f, wantForSlot (toRack, toIndex, *f));
        library.noteUsed (f->key);
    }
    else if (auto type = engine.getKnownPlugins().getTypeForIdentifierString (pluginId.fromFirstOccurrenceOf ("id:", false, false)))
    {
        desc = *type;
    }
    else
    {
        error = "That plugin isn't in the plugin list";
        return false;
    }

    if (! engine.addPlugin (toRack, desc, error))
        return false;

    const int last = engine.getNumSlots (toRack) - 1;
    engine.moveSlotTo (toRack, last, toIndex);
    fixStereoVersions (toRack);
    select (toRack, juce::jlimit (0, last, toIndex));
    return true;
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
    m.addItem (9, "Recalled by new snapshots", true, engine.isSlotInScope (rack, slot));
    if (engine.getSnapshotInfo (currentScene) != nullptr && engine.snapshotHasSlot (currentScene, rack, slot))
        m.addItem (11, "Recalled by snapshot " + juce::String (currentScene), true, engine.isSlotInScopeFor (currentScene, rack, slot));
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
    const bool isolated = engine.isSlotIsolated (rack, slot);
    m.addItem (12, "Run in its own process (experimental)", true, isolated);
    if (isolated && juce::ModifierKeys::currentModifiers.isAltDown())
        m.addItem (13, "Crash this plugin (test)");
    m.addSeparator();
    m.addItem (5, "Remove");

    auto options = screenArea.isEmpty() ? juce::PopupMenu::Options()
                                        : juce::PopupMenu::Options().withTargetScreenArea (screenArea);

    juce::Component::SafePointer<MainComponent> safe (this);
    const int scene = currentScene;
    m.showMenuAsync (options, [safe, rack, slot, slotId, hot, scene] (int result)
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
            case 11: e.setSlotInScopeFor (scene, rack, slot, ! e.isSlotInScopeFor (scene, rack, slot)); break;
            case 10:
                safe->dock.releaseEditor();
                e.setStereoPoint (rack, slot);
                safe->fixStereoVersions (rack);
                safe->refreshAll();
                break;
            case 12:
            {
                const bool wanted = ! e.isSlotIsolated (rack, slot);
                const auto name = e.getPlugin (rack, slot) != nullptr ? e.getPlugin (rack, slot)->getName() : juce::String();
                safe->dock.releaseEditor();
                safe->closeEditorFor (e.getPlugin (rack, slot));
                juce::String error;
                if (! e.setSlotIsolated (rack, slot, wanted, error))
                    safe->consoleNote = "Couldn't move " + name + (wanted ? " to its own process: " : " back into LiveRack: ") + error;
                else
                    safe->consoleNote = name + (wanted ? " now runs in its own process" : " now runs inside LiveRack");
                safe->strips.forgetMeters();
                safe->refreshAll();
                safe->select (rack, slot);
                break;
            }
            case 13: e.crashIsolatedSlot (rack, slot); break;
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
void MainComponent::setFullView (bool shouldBeFull)
{
    fullView = shouldBeFull && ! isTwoWindowMode();
    dock.setFullView (fullView, ! isTwoWindowMode());
    resized();
    repaint();
}

//==============================================================================
// Setup screens shown in the main window, in place of the racks
void MainComponent::attachTool (ToolWindow* tool)
{
    tools.erase (std::remove (tools.begin(), tools.end(), tool), tools.end());
    tools.push_back (tool); // the last one is in front

    if (tool->getFrame().getParentComponent() != this)
        addChildComponent (tool->getFrame());

    showFrontTool();
}

void MainComponent::detachTool (ToolWindow* tool)
{
    tools.erase (std::remove (tools.begin(), tools.end(), tool), tools.end());

    if (tool->getFrame().getParentComponent() == this)
        removeChildComponent (&tool->getFrame());

    showFrontTool();
}

bool MainComponent::dockIsCovered() const
{
    return ! tools.empty() && ! isTwoWindowMode();
}

void MainComponent::showFrontTool()
{
    for (auto* t : tools)
        t->getFrame().setVisible (t == tools.back());

    // Plugin editors are native views that would draw over the screen in front
    const bool covered = dockIsCovered();
    if (covered)
        dock.releaseEditor();
    else if (dockWasCovered)
        dock.show (selectedRack, selectedSlot);
    dockWasCovered = covered;

    // The toolbar buttons are updated once the screen has been stored
    juce::Component::SafePointer<MainComponent> safe (this);
    juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->updateToolButtons(); });

    resized();
    repaint();
}

void MainComponent::updateToolButtons()
{
    // The toolbar button of the screen in front lights up
    auto front = [this] (const std::unique_ptr<ToolWindow>& w) { return w != nullptr && ! tools.empty() && tools.back() == w.get(); };
    devicesButton.setToggleState (front (devicesWindow), juce::dontSendNotification);
    routingButton.setToggleState (front (routingWindow), juce::dontSendNotification);
    scopeButton.setToggleState (front (scopeWindow), juce::dontSendNotification);
    scanButton.setToggleState (front (pluginsWindow), juce::dontSendNotification);
}

// A toolbar button opens its screen, or goes back to the racks if it's already in front
void MainComponent::toggleTool (std::unique_ptr<ToolWindow>& tool, std::function<void()> open)
{
    if (tool != nullptr && ! tool->isPoppedOut() && ! tools.empty() && tools.back() == tool.get())
        tool->close();
    else
        open();
}

void MainComponent::setTwoWindowMode (bool shouldBeTwo)
{
    if (shouldBeTwo == isTwoWindowMode())
        return;

    // The rack window replaces Full View
    if (shouldBeTwo)
        fullView = false;

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
        windowModeButton.setTooltip ("Back to one window");
    }
    else
    {
        AppSettings::set ("rackWindow", rackWindow->getWindowStateAsString());
        rackWindow->removeKeyListener (this);
        rackWindow->clearContentComponent();
        rackWindow.reset();
        addAndMakeVisible (dock);
        windowModeButton.setTooltip ("Rack window: the plugin panel in its own window");
    }

    windowModeButton.setToggleState (shouldBeTwo, juce::dontSendNotification);
    AppSettings::set ("twoWindows", shouldBeTwo ? "1" : "0");
    dock.setFullView (false, ! shouldBeTwo); // Full View is for the main window only
    resized();
    refreshAll(); // shows the selected plugin again in its new home
}

//==============================================================================
// Floating editor windows
void MainComponent::closeEditorFor (juce::AudioProcessor* p)
{
    lingeringWindows.erase (std::remove (lingeringWindows.begin(), lingeringWindows.end(), p), lingeringWindows.end());

    // Anything showing this window on the web remote ends first
    for (auto it = remoteViews.begin(); it != remoteViews.end();)
    {
        if (it->plugin == p)
        {
            if (auto c = it->capture.lock())
                c->windowClosed ("The plugin window was closed on the LiveRack computer.");
            it = remoteViews.erase (it);
        }
        else
            ++it;
    }

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
    lingeringWindows.clear();
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
    devicesWindow = std::make_unique<ToolWindow> (*this, "Devices", devicesPanel, "devicesWindow", [safe]
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

        routingWindow = std::make_unique<ToolWindow> (*this, "Routing", routingMatrix, "routingWindow", [safe]
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
    scopeEditor = new ScopeEditor (engine, engine.getSnapshotInfo (currentScene) != nullptr ? currentScene : -1);
    scopeWindow = std::make_unique<ToolWindow> (*this, "Snapshot Scope", scopeEditor, "scopeWindow", [safe]
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
    if (pluginsWindow != nullptr)
    {
        pluginsWindow->toFront (true);
        return;
    }

    auto* list = new juce::PluginListComponent (engine.getFormatManager(),
                                                engine.getKnownPlugins(),
                                                engine.getDeadMansPedalFile(),
                                                nullptr, false);
    list->setNumberOfThreadsForScanning (1); // keep the UI responsive while scanning
    list->setSize (760, 520);

    juce::Component::SafePointer<MainComponent> safe (this);
    pluginsWindow = std::make_unique<ToolWindow> (*this, "Plugins", list, "pluginsWindow", [safe]
    {
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->pluginsWindow.reset(); });
    });
}

//==============================================================================
// The show file
juce::String MainComponent::showName() const
{
    const auto f = engine.getShowFile();
    return f == juce::File() ? juce::String ("Untitled show") : f.getFileNameWithoutExtension();
}

void MainComponent::updateWindowTitle()
{
    const auto title = "LiveRack " + juce::String (JUCE_APPLICATION_VERSION_STRING) + " - " + showName() + (engine.isShowEdited() ? " (edited)" : "");
    if (title != shownTitle)
        if (auto* w = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent()))
        {
            w->setName (title);
            shownTitle = title;
        }
}

// Whether the rack window is open, and where: kept in the show
void MainComponent::storeWindowLayout()
{
    juce::XmlElement x ("WINDOWS");
    x.setAttribute ("twoWindows", isTwoWindowMode());
    x.setAttribute ("rackWindow", rackWindow != nullptr ? rackWindow->getWindowStateAsString() : AppSettings::get ("rackWindow"));
    engine.setShowExtra (x);
}

void MainComponent::applyWindowLayout (const juce::XmlElement* x)
{
    if (x == nullptr)
        return;

    const auto state = x->getStringAttribute ("rackWindow");
    if (state.isNotEmpty())
        AppSettings::set ("rackWindow", state);

    const bool two = x->getBoolAttribute ("twoWindows", false);
    if (two && rackWindow != nullptr && state.isNotEmpty())
        rackWindow->restoreWindowStateFromString (state);

    setTwoWindowMode (two);
}

void MainComponent::saveShow (bool askForFile)
{
    auto write = [this] (const juce::File& file)
    {
        const auto f = file.withFileExtension ("liverack");
        storeWindowLayout();
        auto xml = engine.createShow();

        if (xml != nullptr)
            backUpShowFile (f);

        if (xml == nullptr || ! xml->writeTo (f))
        {
            showError ("Couldn't save the show", f.getFullPathName());
            return;
        }

        engine.setShowFile (f);
        engine.setShowEdited (false);
        engine.saveSession();
        ignoreEditsUntil = juce::Time::getMillisecondCounter() + 500;
        consoleNote = "Saved " + f.getFileName();
        updateWindowTitle();
        repaint();
    };

    if (! askForFile && engine.getShowFile() != juce::File())
    {
        write (engine.getShowFile());
        return;
    }

    const auto start = engine.getShowFile() != juce::File()
        ? engine.getShowFile()
        : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Show.liverack");

    fileChooser = std::make_unique<juce::FileChooser> ("Save show", start, "*.liverack");
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::warnAboutOverwriting,
        [write] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f != juce::File())
                write (f);
        });
}

// Before a save writes over a show file, the old file is copied into a
// "<Show> Backups" folder next to it. The newest 20 are kept.
void MainComponent::backUpShowFile (const juce::File& f)
{
    if (! f.existsAsFile())
        return;

    const auto folder = f.getParentDirectory().getChildFile (f.getFileNameWithoutExtension() + " Backups");
    if (! folder.createDirectory())
        return;

    auto backups = folder.findChildFiles (juce::File::findFiles, false, "*.liverack");
    std::sort (backups.begin(), backups.end(),
               [] (const juce::File& a, const juce::File& b) { return a.getFileName() > b.getFileName(); });

    // Nothing changed since the newest backup: don't make another
    if (! backups.isEmpty() && backups[0].hasIdenticalContentTo (f))
        return;

    const auto stamp = f.getLastModificationTime().formatted ("%Y-%m-%d %H%M%S");
    if (! f.copyFileTo (folder.getChildFile (f.getFileNameWithoutExtension() + " " + stamp + ".liverack")))
        return;

    backups = folder.findChildFiles (juce::File::findFiles, false, "*.liverack");
    std::sort (backups.begin(), backups.end(),
               [] (const juce::File& a, const juce::File& b) { return a.getFileName() > b.getFileName(); });
    for (int i = 20; i < backups.size(); ++i)
        backups.getReference (i).deleteFile();
}

// Asks to save an edited show first; then does what was asked
void MainComponent::checkUnsavedThen (std::function<void()> next)
{
    if (! engine.isShowEdited())
    {
        next();
        return;
    }

    juce::Component::SafePointer<MainComponent> safe (this);
    auto options = juce::MessageBoxOptions::makeOptionsYesNoCancel (juce::MessageBoxIconType::QuestionIcon,
        "Save " + showName() + " first?", "The show has changes that aren't saved.",
        "Save", "Don't Save", "Cancel", this);

    juce::AlertWindow::showAsync (options, [safe, next] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        if (result == 1)
        {
            if (safe->engine.getShowFile() == juce::File())
            {
                safe->saveShow (true); // pick a file; the user can carry on after
                return;
            }
            safe->saveShow (false);
        }
        next();
    });
}

void MainComponent::openShow()
{
    juce::Component::SafePointer<MainComponent> safe (this);
    checkUnsavedThen ([safe]
    {
        if (safe == nullptr)
            return;

        const auto start = safe->engine.getShowFile() != juce::File()
            ? safe->engine.getShowFile().getParentDirectory()
            : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

        safe->fileChooser = std::make_unique<juce::FileChooser> ("Open show", start, "*.liverack");
        safe->fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe] (const juce::FileChooser& fc)
            {
                const auto f = fc.getResult();
                if (safe != nullptr && f.existsAsFile())
                    safe->openShowFile (f);
            });
    });
}

void MainComponent::openShowFile (const juce::File& f)
{
    auto xml = juce::parseXML (f);

    if (xml == nullptr || ! xml->hasTagName ("LIVERACK"))
    {
        showError ("Couldn't open the show", f.getFullPathName());
        return;
    }

    // Same racks and plugins: recalled in place, editors stay open.
    // Otherwise everything is rebuilt, so editors must close first.
    const bool inPlace = engine.snapshotMatchesCurrentLayout (*xml);

    if (! inPlace)
        closeAllEditors();

    juce::String error;
    if (! engine.loadShow (*xml, error))
        showError ("Show opened with problems", error);

    engine.setShowFile (f);
    afterShowLoaded();
    applyWindowLayout (engine.getShowExtra ("WINDOWS"));

    if (! inPlace)
        select (0, 0);

    consoleNote = "Opened " + f.getFileName()
                + (engine.getSnapshots().empty() ? juce::String()
                                                 : ", " + juce::String ((int) engine.getSnapshots().size()) + " snapshots");
}

void MainComponent::newShow()
{
    juce::Component::SafePointer<MainComponent> safe (this);
    checkUnsavedThen ([safe] { if (safe != nullptr) safe->startNewShow(); });
}

void MainComponent::startNewShow()
{
    closeAllEditors();
    engine.newShow();
    RackConfig first;
    first.name = "Rack 1";
    first.stereo = true;
    engine.addRack (first);
    afterShowLoaded();
    select (0, 0);
    consoleNote = "New show";
}

void MainComponent::afterShowLoaded()
{
    engine.setShowEdited (false);
    ignoreEditsUntil = juce::Time::getMillisecondCounter() + 500;
    currentScene = -1;
    keysBar.load();
    refreshAll();
    updateWindowTitle();
    repaint();
}

void MainComponent::showShowMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader (showName());
    m.addItem (1, "Save", true);
    m.addItem (2, "Save As...");
    m.addItem (3, "Open...");
    m.addSeparator();
    m.addItem (4, "New Show");
    m.addSeparator();
    m.addItem (-1, "LiveRack " + juce::String (JUCE_APPLICATION_VERSION_STRING), false);

    juce::Component::SafePointer<MainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&loadButton), [safe] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1) safe->saveShow (false);
        if (r == 2) safe->saveShow (true);
        if (r == 3) safe->openShow();
        if (r == 4) safe->newShow();
    });
}

// Snapshots: click the scene chip
void MainComponent::showSnapshotMenu()
{
    juce::PopupMenu m;
    const auto& list = engine.getSnapshots();

    if (list.empty())
        m.addItem (-1, "No snapshots in this show yet", false);

    for (auto& s : list)
        m.addItem (1000 + s.number, juce::String (s.number) + (s.name.isNotEmpty() ? "   " + s.name : juce::String()),
                   true, s.number == currentScene);

    m.addSeparator();
    m.addItem (1, "Store Snapshot...");
    const bool has = engine.getSnapshotInfo (currentScene) != nullptr;
    const auto num = has ? " " + juce::String (currentScene) : juce::String();
    m.addItem (2, "Rename Snapshot" + num + "...", has);
    m.addItem (3, "Delete Snapshot" + num, has);
    m.addItem (4, "Scope of Snapshot" + num + "...", has);

    juce::Component::SafePointer<MainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (sceneChip)),
        [safe] (int r)
        {
            if (safe == nullptr || r == 0) return;
            if (r >= 1000) { safe->recallScene (r - 1000); return; }
            if (r == 1) safe->showStoreSceneDialog();
            if (r == 4) { safe->scopeWindow.reset(); safe->scopeEditor = nullptr; safe->showScope(); }
            if (r == 3)
            {
                const int number = safe->currentScene;
                safe->confirm ("Delete " + safe->snapshotLabel (number) + "?",
                               "Its plugin settings are removed from the show.", "Delete",
                               [safe, number]
                               {
                                   if (safe == nullptr) return;
                                   safe->engine.deleteSnapshot (number);
                                   safe->consoleNote = "Deleted snapshot " + juce::String (number);
                                   safe->repaint();
                               });
            }
            if (r == 2)
            {
                auto* w = new juce::AlertWindow ("Rename Snapshot " + juce::String (safe->currentScene), {}, juce::MessageBoxIconType::NoIcon);
                if (auto* info = safe->engine.getSnapshotInfo (safe->currentScene))
                    w->addTextEditor ("name", info->name, "Name");
                w->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
                w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                const int number = safe->currentScene;
                w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, number] (int res)
                {
                    if (safe != nullptr && res == 1)
                        safe->engine.renameSnapshot (number, w->getTextEditorContents ("name"));
                }), true);
            }
            safe->repaint();
        });
}

void MainComponent::mouseUp (const juce::MouseEvent& e)
{
    if (sceneChip.contains (e.getPosition()))
        showSnapshotMenu();
}

juce::String MainComponent::snapshotLabel (int number) const
{
    auto* info = engine.getSnapshotInfo (number);
    return "Snapshot " + juce::String (number)
         + (info != nullptr && info->name.isNotEmpty() ? " (" + info->name + ")" : juce::String());
}

void MainComponent::confirm (const juce::String& title, const juce::String& message,
                             const juce::String& okText, std::function<void()> onOk)
{
    auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon,
                                                                 title, message, okText, "Cancel", this);
    juce::AlertWindow::showAsync (options, [onOk] (int result) { if (result == 1 && onOk != nullptr) onOk(); });
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
    const int suggested = juce::jmax (0, console != nullptr && console->getCurrentScene() >= 0 ? console->getCurrentScene()
                                                                                              : currentScene);
    juce::String name;
    if (auto* info = engine.getSnapshotInfo (suggested))
        name = info->name;

    sceneDialog = std::make_unique<juce::AlertWindow> ("Store Snapshot",
        "Stores the current plugin settings and bypass in the show. When the console recalls this scene "
        "number, LiveRack recalls them. Racks and plugins stay as they are.",
        juce::MessageBoxIconType::NoIcon);

    sceneDialog->addTextEditor ("scene", juce::String (suggested), "Scene number");
    sceneDialog->addTextEditor ("name", name, "Name (optional)");
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
    juce::Component::SafePointer<MainComponent> safe (this);

    if (sceneDialog != nullptr && result == 1)
    {
        const int number = juce::jmax (0, sceneDialog->getTextEditorContents ("scene").getIntValue());
        const auto name = sceneDialog->getTextEditorContents ("name");

        if (engine.getSnapshotInfo (number) != nullptr)
            confirm ("Replace " + snapshotLabel (number) + "?",
                     "The snapshot already exists. Storing replaces its plugin settings with the current ones.", "Replace",
                     [safe, number, name] { if (safe != nullptr) safe->storeScene (number, name); });
        else
            storeScene (number, name);
    }

    juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->sceneDialog.reset(); });
}

void MainComponent::storeScene (int scene, const juce::String& name)
{
    engine.storeSnapshot (scene, name);
    currentScene = scene;
    auto* info = engine.getSnapshotInfo (scene);
    consoleNote = "Stored snapshot " + juce::String (scene)
                + (info != nullptr && info->name.isNotEmpty() ? " (" + info->name + ")" : juce::String());
    repaint();
}

void MainComponent::recallScene (int scene)
{
    currentScene = scene;
    juce::String note;

    if (engine.getSnapshotInfo (scene) == nullptr)
    {
        // A scene file from before 0.13 in the console's scene folder becomes
        // a snapshot in the show the first time it's recalled
        auto* console = primaryConsole();
        const auto file = console != nullptr ? console->fileForScene (scene) : juce::File();
        auto xml = file.existsAsFile() ? juce::parseXML (file) : nullptr;

        juce::String error;
        if (xml == nullptr)
        {
            consoleNote = "Scene " + juce::String (scene) + ": no snapshot stored, racks unchanged";
            repaint();
            return;
        }

        auto name = file.getFileNameWithoutExtension().trimCharactersAtStart ("0123456789").trim();
        if (! engine.importSceneFile (*xml, scene, name, error))
        {
            consoleNote = "Scene " + juce::String (scene) + ": " + file.getFileName() + " not imported, " + error;
            repaint();
            return;
        }
        note = "imported from " + file.getFileName();
    }

    juce::String missing;
    engine.recallSnapshot (scene, missing);

    auto* info = engine.getSnapshotInfo (scene);
    consoleNote = "Snapshot " + juce::String (scene)
                + (info != nullptr && info->name.isNotEmpty() ? " (" + info->name + ")" : juce::String()) + " recalled";
    if (note.isNotEmpty())    consoleNote << ", " << note;
    if (missing.isNotEmpty()) consoleNote << ". " << missing;
    repaint();
}

// Next / previous snapshot in the show (or, until imported, in the scene folder)
void MainComponent::stepScene (int direction)
{
    juce::Array<int> numbers;
    for (auto& s : engine.getSnapshots())
        numbers.addIfNotAlreadyThere (s.number);

    if (auto* console = primaryConsole(); console != nullptr && console->getSettings().sceneFolder.isDirectory())
        for (auto& f : console->getSettings().sceneFolder.findChildFiles (juce::File::findFiles, false, "*.liverack"))
        {
            const auto digits = f.getFileNameWithoutExtension().initialSectionContainingOnly ("0123456789");
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
    shortcutsWindow = std::make_unique<ToolWindow> (*this, "Keyboard Shortcuts", shortcutsEditor, "shortcutsWindow", [safe]
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
    if (auto* info = engine.getSnapshotInfo (currentScene))
        r->setProperty ("sceneName", info->name);
    r->setProperty ("show", showName());
    r->setProperty ("version", juce::String (JUCE_APPLICATION_VERSION_STRING));
    r->setProperty ("bpm", engine.getBpm());
    r->setProperty ("page", engine.getCurrentPage());
    r->setProperty ("selected", selectedRack);
    r->setProperty ("selectedSlot", selectedSlot);
    r->setProperty ("pluginWindows", PluginViewCapture::isSupported());
    r->setProperty ("note", consoleNote);
    r->setProperty ("edited", engine.isShowEdited());
    r->setProperty ("hasShowFile", engine.getShowFile() != juce::File());

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
        ro->setProperty ("stereo", cfg.stereo);
        ro->setProperty ("m2s", cfg.isMonoToStereo());
        ro->setProperty ("driver", cfg.driver);
        ro->setProperty ("in", juce::Array<juce::var> { cfg.input[0], cfg.input[1] });
        ro->setProperty ("out", juce::Array<juce::var> { cfg.output[0], cfg.output[1] });
        ro->setProperty ("console", cfg.consoleChannel);

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

    addWebEditState (*r, query);
    return juce::JSON::toString (root, true);
}

//==============================================================================
// Building the show from the web remote, for a server nobody sits at
juce::File MainComponent::getShowsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("LiveRack Shows");
}

juce::Array<juce::File> MainComponent::getRemoteShowFolders() const
{
    juce::Array<juce::File> folders;
    folders.add (getShowsFolder());

    const auto current = engine.getShowFile().getParentDirectory();
    if (engine.getShowFile() != juce::File() && current != getShowsFolder())
        folders.add (current);

    return folders;
}

void MainComponent::addWebEditState (juce::DynamicObject& r, const juce::String& query)
{
    auto has = [&query] (const juce::String& flag)
    {
        return juce::StringArray::fromTokens (query, "&", "").contains (flag + "=1");
    };

    auto obj = [] { return juce::var (new juce::DynamicObject()); };

    if (has ("edit"))
    {
        juce::Array<juce::var> drivers;
        for (int d = 0; d < engine.getNumDrivers(); ++d)
        {
            const auto st = engine.getDriverStatus (d);
            auto dv = obj();
            auto* o = dv.getDynamicObject();
            o->setProperty ("name", st.open ? st.deviceName : "Driver " + juce::String (d + 1) + " (no device)");
            o->setProperty ("open", st.open);

            juce::Array<juce::var> ins, outs;
            if (auto* dev = engine.getDeviceManager (d).getCurrentAudioDevice())
            {
                const auto inNames = dev->getInputChannelNames(), outNames = dev->getOutputChannelNames();
                for (auto& n : inNames)  ins.add (n);
                for (auto& n : outNames) outs.add (n);
            }
            o->setProperty ("ins", ins);
            o->setProperty ("outs", outs);
            drivers.add (dv);
        }
        r.setProperty ("drivers", drivers);

        // Shows the remote can open: the shows folder, and the current show's folder
        juce::Array<juce::var> shows;
        const auto folders = getRemoteShowFolders();
        for (int fi = 0; fi < folders.size(); ++fi)
        {
            auto files = folders[fi].findChildFiles (juce::File::findFiles, false, "*.liverack");
            std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                       { return a.getLastModificationTime() > b.getLastModificationTime(); });
            for (auto& f : files)
            {
                auto sv = obj();
                sv.getDynamicObject()->setProperty ("name", f.getFileNameWithoutExtension());
                sv.getDynamicObject()->setProperty ("folder", fi);
                sv.getDynamicObject()->setProperty ("current", f == engine.getShowFile());
                shows.add (sv);
            }
        }
        r.setProperty ("shows", shows);
        r.setProperty ("showsFolder", getShowsFolder().getFullPathName());
    }

    if (has ("lib"))
    {
        juce::Array<juce::var> lib;
        for (auto& f : library.getFamilies())
        {
            auto lv = obj();
            auto* o = lv.getDynamicObject();
            o->setProperty ("key", f.key);
            o->setProperty ("name", f.name);
            o->setProperty ("maker", f.manufacturer);
            o->setProperty ("cat", PluginLibrary::categoryName (f.category));
            lib.add (lv);
        }
        r.setProperty ("library", lib);
    }
}

bool MainComponent::handleWebEditCommand (const juce::String& cmd, const juce::var& c)
{
    const int rack = c.hasProperty ("rack") ? (int) c["rack"] : -1;
    const bool validRack = juce::isPositiveAndBelow (rack, engine.getNumRacks());

    // Rack settings sent from the remote, applied to a config
    auto applyTo = [&c, this] (RackConfig& cfg)
    {
        if (c.hasProperty ("name"))
        {
            cfg.name = c["name"].toString().trim();
            if (cfg.name.isEmpty())
                cfg.name = "Rack";
        }
        if (c.hasProperty ("format"))
        {
            const auto f = c["format"].toString();
            cfg.stereo = f == "stereo";
            cfg.monoToStereo = f == "m2s";
        }
        if (c.hasProperty ("driver"))
            cfg.driver = juce::jlimit (0, juce::jmax (0, engine.getNumDrivers() - 1), (int) c["driver"]);
        for (int ch = 0; ch < 2; ++ch)
        {
            if (auto* in = c["in"].getArray(); in != nullptr && ch < in->size())
                cfg.input[ch] = juce::jmax (0, (int) (*in)[ch]);
            if (auto* out = c["out"].getArray(); out != nullptr && ch < out->size())
                cfg.output[ch] = juce::jmax (0, (int) (*out)[ch]);
        }
        if (c.hasProperty ("console"))
            cfg.consoleChannel = juce::jmax (0, (int) c["console"]);
        if (c.hasProperty ("safe"))
            cfg.recallSafe = (bool) c["safe"];
    };

    if (cmd == "addRack")
    {
        addRack(); // next free channels, joins the current page
        const int r = engine.getNumRacks() - 1;
        auto cfg = engine.getRackConfig (r);
        applyTo (cfg);
        engine.setRackConfig (r, cfg);
        select (r, -1);
        consoleNote = "Added " + cfg.name;
        return true;
    }

    if (cmd == "rackConfig" && validRack)
    {
        const auto before = engine.getRackConfig (rack);
        auto cfg = before;
        applyTo (cfg);

        if (cfg.stereo != before.stereo || cfg.monoToStereo != before.monoToStereo || cfg.driver != before.driver)
        {
            // Plugins are re-prepared: their windows close first
            dock.releaseEditor();
            closeEditorsForRack (rack);
        }

        engine.setRackConfig (rack, cfg);
        if (cfg.isMonoToStereo() != before.isMonoToStereo() || cfg.stereo != before.stereo)
            fixStereoVersions (rack);
        refreshAll();
        return true;
    }

    if (cmd == "removeRack" && validRack)
    {
        consoleNote = "Removed " + engine.getRackConfig (rack).name;
        removeRack (rack);
        return true;
    }

    if (cmd == "addPlugin" && validRack)
    {
        const int at = c.hasProperty ("index") ? (int) c["index"] : engine.getNumSlots (rack);
        juce::String error;
        if (insertPlugin ("family:" + c["key"].toString(), rack, juce::jlimit (0, engine.getNumSlots (rack), at), error))
            consoleNote = "Added " + engine.getPlugin (rack, juce::jlimit (0, engine.getNumSlots (rack) - 1, at))->getName();
        else
            consoleNote = "Couldn't add the plugin" + (error.isNotEmpty() ? ": " + error : juce::String());
        return true;
    }

    const int slot = c.hasProperty ("slot") ? (int) c["slot"] : -1;
    const bool validSlot = validRack && engine.getPlugin (rack, slot) != nullptr;

    if (cmd == "removePlugin" && validSlot)
    {
        consoleNote = "Removed " + engine.getPlugin (rack, slot)->getName();
        removeSlot (rack, slot);
        return true;
    }

    if (cmd == "movePlugin" && validSlot)
    {
        const int to = juce::jlimit (0, engine.getNumSlots (rack) - 1, (int) c["to"]);
        engine.moveSlotTo (rack, slot, to);
        fixStereoVersions (rack);
        select (rack, to);
        return true;
    }

    // Shows. "save": true saves the current show first (the remote asks).
    auto saveFirst = [this, &c]
    {
        if ((bool) c["save"] && engine.getShowFile() != juce::File())
            saveShow (false);
    };

    if (cmd == "showSave")
    {
        const auto name = juce::File::createLegalFileName (c["name"].toString().trim());

        if (name.isNotEmpty())
        {
            getShowsFolder().createDirectory();
            engine.setShowFile (getShowsFolder().getChildFile (name + ".liverack"));
        }
        else if (engine.getShowFile() == juce::File())
        {
            consoleNote = "Give the show a name to save it";
            return true;
        }

        saveShow (false);
        return true;
    }

    if (cmd == "showNew")
    {
        saveFirst();
        startNewShow();
        return true;
    }

    if (cmd == "showOpen")
    {
        const auto folders = getRemoteShowFolders();
        const int fi = (int) c["folder"];
        const auto name = c["name"].toString();

        if (! juce::isPositiveAndBelow (fi, folders.size()) || name.isEmpty() || name.containsAnyOf ("/\\:"))
            return true;

        const auto file = folders[fi].getChildFile (name + ".liverack");
        if (! file.existsAsFile())
        {
            consoleNote = "That show isn't there any more";
            return true;
        }

        saveFirst();
        openShowFile (file);
        return true;
    }

    return false;
}

//==============================================================================
// The plugin's own window on the web remote
std::shared_ptr<PluginViewCapture> MainComponent::openRemoteView (const juce::String& query, juce::String& error)
{
    auto param = [&query] (const juce::String& name)
    {
        for (auto& part : juce::StringArray::fromTokens (query, "&", ""))
            if (part.startsWith (name + "="))
                return juce::URL::removeEscapeChars (part.fromFirstOccurrenceOf ("=", false, false));
        return juce::String();
    };

    const auto viewText = param ("view");
    const int rack = viewText.upToFirstOccurrenceOf (":", false, false).getIntValue();
    const int slot = viewText.fromFirstOccurrenceOf (":", false, false).getIntValue();
    auto* plugin = viewText.containsChar (':') ? engine.getPlugin (rack, slot) : nullptr;

    if (plugin == nullptr)
    {
        error = "That plugin isn't there any more.";
        return nullptr;
    }

    // A plugin in its own process: its helper owns the window and captures it
    if (auto* isolated = dynamic_cast<IsolatedPlugin*> (plugin))
    {
        const auto s = param ("scale"), q = param ("q");
        auto capture = isolated->openRemoteView (s.isNotEmpty() ? juce::jlimit (0.5f, 3.0f, s.getFloatValue()) : 2.0f,
                                                 q.isNotEmpty() ? juce::jlimit (0.2f, 0.95f, q.getFloatValue()) : 0.7f,
                                                 error);
        if (capture != nullptr)
            remoteViews.push_back ({ capture, plugin, false });
        return capture;
    }

    auto findWindow = [this, plugin]() -> PluginWindow*
    {
        for (auto& w : editorWindows)
            if (&w->processor == plugin)
                return w.get();
        return nullptr;
    };

    // The remote shows a plugin's own window, so it needs one
    auto* window = findWindow();

    // Reconnecting after a dropout: the window we opened is still waiting
    const auto lingering = std::find (lingeringWindows.begin(), lingeringWindows.end(), plugin);
    const bool reclaimed = lingering != lingeringWindows.end() && window != nullptr;
    if (lingering != lingeringWindows.end())
        lingeringWindows.erase (lingering);

    const bool opened = window == nullptr || reclaimed;
    if (window == nullptr)
    {
        popOutEditor (rack, slot);
        window = findWindow();
    }

    if (window == nullptr)
    {
        error = "Couldn't open the plugin window.";
        return nullptr;
    }

    window->toFront (false);

    PluginViewCapture::Options options;
    if (auto s = param ("scale"); s.isNotEmpty())
        options.scale = juce::jlimit (0.5f, 3.0f, s.getFloatValue());
    if (auto q = param ("q"); q.isNotEmpty())
        options.quality = juce::jlimit (0.2f, 0.95f, q.getFloatValue());

    auto capture = PluginViewCapture::create (*window, options, error);
    if (capture == nullptr)
    {
        const bool othersUseIt = std::any_of (remoteViews.begin(), remoteViews.end(), [plugin] (auto& v) { return v.plugin == plugin; });
        if (opened && ! othersUseIt)
        {
            closeEditorFor (plugin);
            refreshAll();
        }
        return nullptr;
    }

    remoteViews.push_back ({ capture, plugin, opened });
    return capture;
}

void MainComponent::closeRemoteView (std::shared_ptr<PluginViewCapture> capture)
{
    if (capture == nullptr)
        return;

    capture->stop();

    auto it = std::find_if (remoteViews.begin(), remoteViews.end(),
                            [&capture] (auto& v) { return v.capture.lock() == capture; });
    if (it == remoteViews.end())
        return; // the window already closed

    auto* plugin = it->plugin;
    bool closeWindow = it->openedWindow;
    remoteViews.erase (it);

    // Another device still showing it keeps the window
    for (auto& v : remoteViews)
        if (v.plugin == plugin)
        {
            v.openedWindow = v.openedWindow || closeWindow;
            closeWindow = false;
        }

    if (closeWindow)
    {
        // Give a dropped connection a few seconds to come back before closing
        // the window, so a Wi-Fi blip doesn't make it flash closed and open
        lingeringWindows.push_back (plugin);
        juce::Component::SafePointer<MainComponent> safe (this);
        juce::Timer::callAfterDelay (4000, [safe, plugin]
        {
            if (safe == nullptr)
                return;
            auto& list = safe->lingeringWindows;
            const auto found = std::find (list.begin(), list.end(), plugin);
            if (found == list.end())
                return; // came back, or the window closed anyway
            list.erase (found);
            safe->closeEditorFor (plugin);
            safe->refreshAll(); // dock it again if it's selected
        });
    }
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
    else
        handleWebEditCommand (cmd, c);
}
