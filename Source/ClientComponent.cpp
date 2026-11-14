#include "ClientComponent.h"
#include "AppMode.h"
#include "AppSettings.h"
#include "RemoteConnection.h"
#include "RemotePluginView.h"
#include "ServerFinder.h"
#include "RackStripsView.h"
#include "PluginDock.h"
#include "ControlBars.h"
#include "PluginLibrary.h"
#include "PluginPicker.h"
#include "DevicesPanel.h"
#include "DriverSettingsPanel.h"
#include "ConsoleLinkPanel.h"
#include "RoutingMatrix.h"
#include "SnapshotsPanel.h"
#include "PluginsPanel.h"
#include "Theme.h"

namespace
{
    juce::var makeCmd (const juce::String& name, std::initializer_list<std::pair<const char*, juce::var>> props = {})
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("cmd", name);
        for (auto& p : props)
            o->setProperty (p.first, p.second);
        return juce::var (o);
    }

    // "name|host|port" lines, newest first
    struct RecentServer { juce::String name, host; int port = ClientComponent::defaultPort; };

    std::vector<RecentServer> loadRecentServers()
    {
        std::vector<RecentServer> list;
        juce::StringArray lines;
        lines.addLines (AppSettings::get ("clientServers"));
        for (auto& l : lines)
        {
            auto parts = juce::StringArray::fromTokens (l, "|", "");
            if (parts.size() >= 3 && parts[1].isNotEmpty())
                list.push_back ({ parts[0], parts[1], parts[2].getIntValue() > 0 ? parts[2].getIntValue() : ClientComponent::defaultPort });
        }
        return list;
    }

    void rememberServer (const juce::String& name, const juce::String& host, int port)
    {
        auto list = loadRecentServers();
        list.erase (std::remove_if (list.begin(), list.end(), [&] (const RecentServer& s) { return s.host == host && s.port == port; }), list.end());
        list.insert (list.begin(), { name.isNotEmpty() ? name : host, host, port });
        if (list.size() > 6)
            list.resize (6);
        juce::StringArray lines;
        for (auto& s : list)
            lines.add (s.name.removeCharacters ("|") + "|" + s.host + "|" + juce::String (s.port));
        AppSettings::set ("clientServers", lines.joinIntoString ("\n"));
    }

    // "10.0.0.5", "ryzen.local:8091"
    void parseAddress (const juce::String& text, juce::String& host, int& port)
    {
        auto t = text.trim().fromFirstOccurrenceOf ("://", false, false);
        if (t.isEmpty())
            t = text.trim();
        t = t.upToFirstOccurrenceOf ("/", false, false);
        port = ClientComponent::defaultPort;
        host = t;
        if (t.containsChar (':'))
        {
            host = t.upToLastOccurrenceOf (":", false, false);
            port = t.fromLastOccurrenceOf (":", false, false).getIntValue();
            if (port <= 0)
                port = ClientComponent::defaultPort;
        }
    }

    void drawHeading (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (title, area, juce::Justification::centredLeft);
    }

    void drawLogo (juce::Graphics& g, int toolbarHeight, const juce::String& tag)
    {
        auto logo = juce::Rectangle<float> (16.0f, (float) (toolbarHeight - 28) / 2.0f, 28.0f, 28.0f);
        g.setColour (Theme::accent);
        g.fillRoundedRectangle (logo, 6.0f);
        g.setColour (Theme::background);
        for (int i = 0; i < 3; ++i)
            g.fillRoundedRectangle (logo.getX() + 7.0f, logo.getY() + 8.0f + (float) i * 5.0f, 14.0f, 2.0f, 1.0f);

        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        g.drawText ("LiveRack", 52, 0, 90, toolbarHeight / 2 + 9, juce::Justification::bottomLeft);
        g.setColour (Theme::accent);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (tag, 53, toolbarHeight / 2 + 9, 90, 14, juce::Justification::topLeft);
    }

    void openDialog (juce::Component* content, const juce::String& title, std::vector<juce::Component::SafePointer<juce::Component>>& list)
    {
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (content);
        o.dialogTitle = title;
        o.dialogBackgroundColour = Theme::panel;
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.resizable = true;
        list.push_back (o.launchAsync());
    }
}

//==============================================================================
// "Connect to a LiveRack server": the servers found on the network, the
// recent ones, or an address typed in
class ClientComponent::ConnectScreen : public juce::Component,
                                       private juce::Timer
{
public:
    ConnectScreen (ClientComponent& o, ServerFinder& f) : owner (o), finder (f)
    {
        addAndMakeVisible (address);
        address.setTextToShowWhenEmpty ("Address: name or IP, e.g. 192.168.1.20", Theme::textDim);
        address.setFont (juce::FontOptions (15.0f));
        address.setJustification (juce::Justification::centredLeft);
        address.setIndents (10, 0);
        address.onReturnKey = [this] { connectToTyped(); };
        if (auto recent = loadRecentServers(); ! recent.empty())
            address.setText (recent.front().host + (recent.front().port != defaultPort ? ":" + juce::String (recent.front().port) : juce::String()),
                             juce::dontSendNotification);

        addAndMakeVisible (connectButton);
        connectButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
        connectButton.setColour (juce::TextButton::textColourOffId, Theme::background);
        connectButton.onClick = [this] { connectToTyped(); };

        addAndMakeVisible (nativeButton);
        nativeButton.onClick = [] { if (AppMode::switchTo != nullptr) AppMode::switchTo (false); };

        rebuild();
        startTimer (700);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::background);
        g.setColour (Theme::panel);
        g.fillRect (0, 0, getWidth(), 72);
        g.setColour (Theme::border);
        g.fillRect (0, 72, getWidth(), 1);
        drawLogo (g, 72, "CLIENT");

        auto col = column();
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
        g.drawText ("Connect to a LiveRack server", col.removeFromTop (40), juce::Justification::bottomLeft);
        col.removeFromTop (8);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawFittedText ("The server is LiveRack running on the computer with the audio (Server mode). "
                          "The Client shows its racks and plugins here and controls it over the network. "
                          "The server's web remote must be on (Devices, iPad / web remote).",
                          col.removeFromTop (60), juce::Justification::topLeft, 3);

        for (auto& h : headings)
            drawHeading (g, h.first, h.second);

        if (serverButtons.isEmpty())
        {
            g.setColour (Theme::textDim.withAlpha (0.8f));
            g.setFont (juce::FontOptions (13.5f));
            g.drawText (finder.isListening() ? "Looking for servers on this network..." : "Can't listen for servers here: type the address below.",
                        emptyArea, juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto col = column();
        col.removeFromTop (118);
        headings.clear();

        headings.push_back ({ col.removeFromTop (24), "ON THIS NETWORK" });
        if (serverButtons.isEmpty())
            emptyArea = col.removeFromTop (44);
        for (auto* b : serverButtons)
        {
            b->setBounds (col.removeFromTop (48));
            col.removeFromTop (6);
        }

        if (! recentButtons.isEmpty())
        {
            col.removeFromTop (14);
            headings.push_back ({ col.removeFromTop (24), "RECENT" });
            for (auto* b : recentButtons)
            {
                b->setBounds (col.removeFromTop (40));
                col.removeFromTop (6);
            }
        }

        col.removeFromTop (14);
        headings.push_back ({ col.removeFromTop (24), "ADDRESS" });
        auto row = col.removeFromTop (40);
        connectButton.setBounds (row.removeFromRight (120));
        row.removeFromRight (8);
        address.setBounds (row);

        col.removeFromTop (28);
        nativeButton.setBounds (col.removeFromTop (34).removeFromLeft (360));
    }

private:
    juce::Rectangle<int> column() const
    {
        return getLocalBounds().withTrimmedTop (72).reduced (24, 28).withSizeKeepingCentre (juce::jmin (getWidth() - 48, 640), getHeight() - 72 - 56);
    }

    void timerCallback() override
    {
        // Rebuild when the servers found change (not just their timestamps)
        juce::StringArray now;
        for (auto& s : finder.getServers())
            now.add (s.name + "|" + s.host + "|" + juce::String (s.port) + "|" + s.version);
        if (now != shown)
            rebuild();
    }

    void rebuild()
    {
        serverButtons.clear();
        recentButtons.clear();
        shown.clear();

        for (auto& s : finder.getServers())
        {
            shown.add (s.name + "|" + s.host + "|" + juce::String (s.port) + "|" + s.version);
            auto* b = serverButtons.add (new juce::TextButton (s.name + "     " + s.host
                                                               + (s.port != defaultPort ? ":" + juce::String (s.port) : juce::String())
                                                               + "     LiveRack " + s.version));
            b->setColour (juce::TextButton::buttonColourId, Theme::raised);
            const auto name = s.name, host = s.host;
            const int port = s.port;
            b->onClick = [this, name, host, port] { owner.connectTo (host, port, name); };
            b->setTooltip (s.version != JUCE_APPLICATION_VERSION_STRING
                               ? "This is LiveRack " JUCE_APPLICATION_VERSION_STRING ". Use the same version on both for everything to work."
                               : juce::String());
            addAndMakeVisible (b);
        }

        for (auto& r : loadRecentServers())
        {
            auto* b = recentButtons.add (new juce::TextButton (r.name + "     " + r.host
                                                               + (r.port != defaultPort ? ":" + juce::String (r.port) : juce::String())));
            const auto name = r.name, host = r.host;
            const int port = r.port;
            b->onClick = [this, name, host, port] { owner.connectTo (host, port, name); };
            addAndMakeVisible (b);
        }

        resized();
        repaint();
    }

    void connectToTyped()
    {
        juce::String host;
        int port = defaultPort;
        parseAddress (address.getText(), host, port);
        if (host.isNotEmpty())
            owner.connectTo (host, port, {});
    }

    ClientComponent& owner;
    ServerFinder& finder;
    juce::OwnedArray<juce::TextButton> serverButtons, recentButtons;
    juce::StringArray shown;
    juce::TextEditor address;
    juce::TextButton connectButton { "Connect" },
                     nativeButton { "Run shows on this computer instead (LiveRack Native)" };
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> headings;
    juce::Rectangle<int> emptyArea;
};

//==============================================================================
// One server: the LiveRack screens over the network
class ClientComponent::ClientView : public juce::Component,
                                    public juce::DragAndDropContainer,
                                    public ActionHost,
                                    private juce::ChangeListener,
                                    private juce::Timer,
                                    private juce::KeyListener
{
public:
    ClientView (ClientComponent& o, const juce::String& host, int port, const juce::String& name);
    ~ClientView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    // ActionHost: User Keys and Hot Plugins
    void performAction (const Action&, int value) override;
    bool isActionActive (const Action&) const override;
    int  getSelectedRack() const override { return selectedRack; }
    int  getSelectedSlot() const override { return selectedSlot; }

    RemoteConnection live;
    const juce::String displayName;

private:
    enum class View { channels, rack, devices, routing, plugins, snapshots };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    using juce::Component::keyPressed;
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;

    // State the server sends that the connection doesn't wrap
    const juce::var& state() const { return live.getState(); }
    int currentScene() const      { return state().hasProperty ("scene") ? (int) state()["scene"] : -1; }
    double bpm() const            { const double b = (double) state()["bpm"]; return b > 0.0 ? b : 120.0; }
    void send (const juce::var& c) { live.sendCommand (c); }

    // Selection and views
    void select (int rack, int slot);
    void stepRack (int delta);
    void refreshAll();
    void setView (View);
    void openSlotId (int slotId);
    void updateViewButtons();
    void updateWindowTitle();
    void showLogoMenu();

    // Racks and plugins
    void showRackMenu (int rack);
    void showRackDialog (int rack);
    void changeFormat (int rack, const RackConfig&);
    void addPage();
    void showPageMenu (int page);
    void showAddPluginMenu (int rack);
    void showSlotMenu (int rack, int slot);
    void popOut (int rack, int slot);
    void loadLibrary();

    // Shows and snapshots
    void showShowMenu();
    void saveShowAs();
    void openOrNew (std::function<void (bool saveFirst)> go, const juce::String& what);
    void showSnapshotMenu();
    void showStoreDialog();
    void confirm (const juce::String& title, const juce::String& message, const juce::String& ok, std::function<void()> onOk);

    ClientComponent& owner;
    PluginLibrary library { RackEngine::getSettingsDir().getChildFile ("client-plugin-library.xml") };
    bool libraryLoaded = false, scanWasRunning = false;

    int selectedRack = -1, selectedSlot = -1;
    int serverSelectedRack = -2, serverSelectedSlot = -2;   // last seen, to follow the server's selection
    int racksBefore = -1;
    bool selectNewRack = false;
    View view = View::channels, lastRackView = View::channels;

    Theme::IconButton saveButton      { "Save", "save", "Save the show on the server (Cmd+S)" },
                      loadButton      { "Show", "show", "Open, save as or start a show on the server" },
                      prevSceneButton { "Previous", "prev", "Previous snapshot" },
                      nextSceneButton { "Next", "next", "Next snapshot" },
                      overviewButton  { "Channels", "overview", "All the racks (double-click a plugin to open it in Rack)" },
                      rackViewButton  { "Rack", "rack", "The selected rack's plugins in the whole window (Esc goes back to Channels)" },
                      devicesButton   { "Devices", "devices", "The server's audio drivers, consoles and remotes" },
                      routingButton   { "Routing", "routing", "Patch rack inputs and outputs" },
                      scanButton      { "Plugins", "plugins", "The server's plugins and scanning" },
                      scopeButton     { "Snapshots", "snapshots", "Snapshots: recall, store and what each one recalls" },
                      addRackButton   { "Add Rack", "addrack", "Add a rack" },
                      stackRowsButton { "Stack Rows", "stack", "Stack racks in rows to fit the window" };
    juce::TextButton storeSceneButton { "STORE" }, tapButton { "TAP" };
    std::vector<juce::Rectangle<int>> toolbarGroups;
    juce::Rectangle<int> sceneChip, bpmArea, logoArea;
    static constexpr int toolbarHeight = 72;

    RackStripsView strips { live };
    PluginDock dock { live };
    HotPluginsBar hotBar { live, *this };
    PageTabs pageTabs { live };

    class KeysBar;
    class StatusBar;
    class Overlay;
    std::unique_ptr<KeysBar> keysBar;
    std::unique_ptr<StatusBar> statusBar;
    std::unique_ptr<Overlay> overlay;

    std::unique_ptr<juce::Component> tool;   // Devices, Routing, Plugins or Snapshots
    DevicesPanel* devicesPanel = nullptr;
    RoutingMatrix* routingMatrix = nullptr;
    SnapshotsPanel* snapshotsPanel = nullptr;

    class PickerOverlay;
    std::unique_ptr<PickerOverlay> pickerOverlay;
    std::vector<std::unique_ptr<RemotePluginWindow>> popOuts;
    std::vector<juce::Component::SafePointer<juce::Component>> dialogs;
    juce::String shownTitle;
    int ticks = 0;

   #if LIVERACK_PREVIEW_HOOKS
    friend struct ClientPreview;
   #endif
};

//==============================================================================
// The User Keys, as the server has them: press to run (against this window's
// selection). They're assigned on the server for now.
class ClientComponent::ClientView::KeysBar : public juce::Component
{
public:
    explicit KeysBar (ClientView& v) : owner (v) {}

    static constexpr int numKeys = 8;
    static constexpr int preferredHeight = 64;

    Action actionFor (int i) const
    {
        Action a;
        const auto k = keyState (i);
        if (auto* arr = k["action"].getArray(); arr != nullptr && arr->size() >= 4)
        {
            a.type = (Action::Type) juce::jlimit (0, (int) Action::numTypes - 1, (int) (*arr)[0]);
            a.rack = (int) (*arr)[1];
            a.slotId = (int) (*arr)[2];
            a.number = (int) (*arr)[3];
        }
        return a;
    }

    void paint (juce::Graphics& g) override
    {
        drawHeading (g, getLocalBounds().removeFromTop (16), "USER KEYS");

        for (int i = 0; i < numKeys; ++i)
        {
            const auto k = keyState (i);
            const auto a = actionFor (i);
            const auto r = keyBounds (i).toFloat();
            const bool assigned = a.type != Action::none;
            const bool lit = assigned && owner.isActionActive (a);
            auto fill = lit ? Theme::amber : assigned ? Theme::raised : Theme::panel;
            if (i == pressedKey) fill = fill.brighter (0.15f);

            g.setColour (fill);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (lit ? Theme::amber : Theme::border);
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

            auto inner = r.reduced (8.0f, 5.0f);
            const auto ink = lit ? juce::Colour (0xff101214) : Theme::text;
            inner.removeFromTop (12.0f);
            g.setColour (assigned ? ink : Theme::textDim);
            g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
            g.drawFittedText (assigned ? k["label"].toString() : "Empty", inner.removeFromTop (inner.getHeight() * 0.55f).toNearestInt(),
                              juce::Justification::centred, 1, 0.75f);
            g.setColour (ink.withAlpha (0.7f));
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (k["category"].toString(), inner.toNearestInt(), juce::Justification::centredTop, true);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < numKeys; ++i)
        {
            if (! keyBounds (i).contains (e.getPosition()))
                continue;

            const auto a = actionFor (i);
            if (e.mods.isPopupMenu() || a.type == Action::none)
            {
                juce::PopupMenu m;
                m.addItem (1, "User Keys are assigned on the server for now", false);
                m.showMenuAsync ({});
                return;
            }

            pressedKey = i;
            repaint();
            owner.performAction (a, -1);
            juce::Component::SafePointer<KeysBar> safe (this);
            juce::Timer::callAfterDelay (120, [safe] { if (safe != nullptr) { safe->pressedKey = -1; safe->repaint(); } });
            return;
        }
    }

private:
    juce::var keyState (int i) const
    {
        if (auto* a = owner.state()["keys"].getArray(); a != nullptr && juce::isPositiveAndBelow (i, a->size()))
            return a->getReference (i);
        return {};
    }

    juce::Rectangle<int> keyBounds (int index) const
    {
        auto area = getLocalBounds().reduced (0, 2).withTrimmedTop (18);
        const int gap = 8;
        const int w = (area.getWidth() - gap * (numKeys - 1)) / numKeys;
        return { area.getX() + index * (w + gap), area.getY(), w, area.getHeight() };
    }

    ClientView& owner;
    int pressedKey = -1;
};

//==============================================================================
// Bottom bar: the server, its audio drivers and consoles, the last message
class ClientComponent::ClientView::StatusBar : public juce::Component
{
public:
    explicit StatusBar (ClientView& v) : owner (v) {}

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff0d0f11));
        g.setColour (Theme::border);
        g.fillRect (0, 0, getWidth(), 1);

        auto area = getLocalBounds().reduced (10, 4);
        auto& live = owner.live;

        {
            const juce::String version ("LiveRack Client " JUCE_APPLICATION_VERSION_STRING);
            g.setFont (juce::FontOptions (11.5f));
            g.setColour (Theme::textDim);
            g.drawText (version, area.removeFromRight (juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (11.5f), version) + 4),
                        juce::Justification::centredRight);
            area.removeFromRight (10);
        }

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

        // The server
        const auto serverVersion = live.getServerVersion();
        const bool mismatch = serverVersion.isNotEmpty() && serverVersion != JUCE_APPLICATION_VERSION_STRING;
        chip (! live.isConnected() ? Theme::red : mismatch ? Theme::amber : Theme::green, live.getServerName(),
              ! live.isConnected() ? juce::String ("not connected")
                                   : mismatch ? "LiveRack " + serverVersion + ": update so both match"
                                              : live.getHost(), false);

        for (int d = 0; d < live.getNumDrivers() && live.isConnected(); ++d)
        {
            const auto s = live.getDriverStatus (d);
            if (! s.open)
            {
                chip (Theme::red, "Driver " + juce::String (d + 1), "no device: open Devices", false);
                continue;
            }
            const bool recentDrop = s.dropouts > 0 && juce::Time::getCurrentTime() - s.lastDropout < juce::RelativeTime::minutes (5);
            juce::String rest;
            rest << juce::String (s.sampleRate / 1000.0, 1) << " kHz  " << s.bufferSize << " smp  DSP "
                 << juce::String ((int) std::ceil (s.cpu * 100.0)) << "%";
            if (s.dropouts >= 0)
                rest << "  " << s.dropouts << (s.dropouts == 1 ? " drop" : " drops");
            chip (recentDrop ? Theme::amber : Theme::green, s.deviceName.substring (0, 28), rest, false);
        }

        for (int i = live.getNumConsoles(); --i >= 0;)
        {
            const auto c = live.getConsole (i);
            if (c.settings.type == ConsoleLink::Type::off)
                continue;
            chip (c.connected ? Theme::green : Theme::red, c.displayName,
                  c.connected ? (c.currentScene >= 0 ? "scene " + juce::String (c.currentScene) : juce::String ("linked")) : "not connected", true);
        }

        const auto note = owner.state()["note"].toString();
        if (note.isNotEmpty())
        {
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (note, area, juce::Justification::centredLeft, true);
        }
    }

    void mouseUp (const juce::MouseEvent&) override { owner.setView (View::devices); }

private:
    ClientView& owner;
};

//==============================================================================
// Over everything while the server can't be reached
class ClientComponent::ClientView::Overlay : public juce::Component
{
public:
    explicit Overlay (ClientView& v) : owner (v)
    {
        addAndMakeVisible (otherButton);
        otherButton.onClick = [this] { auto& o = owner.owner; juce::MessageManager::callAsync ([&o] { o.showConnectScreen(); }); };
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::background.withAlpha (0.85f));
        auto box = box_();
        g.setColour (Theme::panel);
        g.fillRoundedRectangle (box.toFloat(), 10.0f);
        g.setColour (Theme::border);
        g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 10.0f, 1.0f);

        auto t = box.reduced (24, 20);
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        const bool ever = owner.live.hasEverConnected();
        const auto name = owner.live.hasEverConnected() ? owner.live.getServerName() : owner.displayName;
        g.drawText (ever ? "Lost the connection to " + name : "Connecting to " + name + "...",
                    t.removeFromTop (30), juce::Justification::centredLeft, true);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawFittedText (ever ? "Trying again. The audio keeps running on the server; only this screen is cut off."
                               : "Looking for LiveRack at " + owner.live.getHost() + ":" + juce::String (owner.live.getPort())
                                 + ". Check that LiveRack is running there with its web remote on, and that both computers are on the same network.",
                          t.removeFromTop (60), juce::Justification::topLeft, 3);
    }

    void resized() override
    {
        otherButton.setBounds (box_().reduced (24, 20).removeFromBottom (34).removeFromLeft (220));
    }

private:
    juce::Rectangle<int> box_() const { return getLocalBounds().withSizeKeepingCentre (juce::jmin (getWidth() - 40, 560), 190); }

    ClientView& owner;
    juce::TextButton otherButton { "Choose another server" };
};

//==============================================================================
class ClientComponent::ClientView::PickerOverlay : public juce::Component
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

//==============================================================================
ClientComponent::ClientView::ClientView (ClientComponent& o, const juce::String& host, int port, const juce::String& name)
    : live (host, port), displayName (name.isNotEmpty() ? name : host), owner (o)
{
    keysBar = std::make_unique<KeysBar> (*this);
    statusBar = std::make_unique<StatusBar> (*this);

    for (juce::Button* b : { (juce::Button*) &saveButton, (juce::Button*) &loadButton, (juce::Button*) &prevSceneButton,
                             (juce::Button*) &nextSceneButton, (juce::Button*) &storeSceneButton, (juce::Button*) &overviewButton,
                             (juce::Button*) &rackViewButton, (juce::Button*) &devicesButton, (juce::Button*) &routingButton,
                             (juce::Button*) &scanButton, (juce::Button*) &scopeButton, (juce::Button*) &addRackButton,
                             (juce::Button*) &stackRowsButton, (juce::Button*) &tapButton })
        addAndMakeVisible (b);

    addAndMakeVisible (hotBar);
    addAndMakeVisible (pageTabs);
    addAndMakeVisible (strips);
    addChildComponent (dock);
    addAndMakeVisible (*keysBar);
    addAndMakeVisible (*statusBar);

    saveButton.setLabel ("Save");
    loadButton.setLabel ("Show");
    overviewButton.setLabel ("Channels");
    rackViewButton.setLabel ("Rack");
    devicesButton.setLabel ("Devices");
    routingButton.setLabel ("Routing");
    scanButton.setLabel ("Plugins");
    scopeButton.setLabel ("Snapshots");
    addRackButton.setLabel ("Add Rack");
    stackRowsButton.setLabel ("Stack Rows");

    saveButton.onClick = [this]
    {
        if ((bool) state()["hasShowFile"]) send (makeCmd ("showSave"));
        else                              saveShowAs();
    };
    loadButton.onClick      = [this] { showShowMenu(); };
    prevSceneButton.onClick = [this] { send (makeCmd ("sceneStep", { { "dir", -1 } })); };
    nextSceneButton.onClick = [this] { send (makeCmd ("sceneStep", { { "dir", 1 } })); };
    storeSceneButton.onClick = [this] { showStoreDialog(); };
    storeSceneButton.setTooltip ("Store a snapshot of the plugin settings in the show");
    overviewButton.onClick  = [this] { setView (View::channels); };
    rackViewButton.onClick  = [this] { setView (view == View::rack ? View::channels : View::rack); };
    devicesButton.onClick   = [this] { setView (view == View::devices ? lastRackView : View::devices); };
    routingButton.onClick   = [this] { setView (view == View::routing ? lastRackView : View::routing); };
    scanButton.onClick      = [this] { setView (view == View::plugins ? lastRackView : View::plugins); };
    scopeButton.onClick     = [this] { setView (view == View::snapshots ? lastRackView : View::snapshots); };
    addRackButton.onClick   = [this] { selectNewRack = true; send (makeCmd ("addRack")); };
    tapButton.onClick       = [this] { send (makeCmd ("tap")); };
    tapButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
    tapButton.setColour (juce::TextButton::textColourOffId, Theme::background);

    const bool stack = AppSettings::get ("stackRows") != "0";
    stackRowsButton.setClickingTogglesState (true);
    stackRowsButton.setToggleState (stack, juce::dontSendNotification);
    strips.setStackRows (stack);
    stackRowsButton.onClick = [this]
    {
        strips.setStackRows (stackRowsButton.getToggleState());
        AppSettings::set ("stackRows", stackRowsButton.getToggleState() ? "1" : "0");
    };

    strips.onSelect    = [this] (int r, int s) { select (r, s); };
    strips.onAddPlugin = [this] (int r) { showAddPluginMenu (r); };
    strips.onSlotMenu  = [this] (int r, int s) { showSlotMenu (r, s); };
    strips.onRackMenu  = [this] (int r) { showRackMenu (r); };
    strips.onEditRack  = [this] (int r) { showRackDialog (r); };
    strips.onOpenSlot  = [this] (int r, int s) { select (r, s); setView (View::rack); };
    strips.onAddRack   = [this] { selectNewRack = true; send (makeCmd ("addRack")); };
    strips.onOpenRouting = [this] (int r) { select (r, -1); setView (View::routing); };
    strips.onEditConsoleChannel = [this] (int r) { showRackDialog (r); };
    strips.onFormatChange = [this] (int r, const RackConfig& c) { changeFormat (r, c); };
    strips.onSlotDropped = [this] (int fr, int fs, int tr, int ti, bool copy)
    {
        send (makeCmd ("slotDrop", { { "rack", fr }, { "slot", fs }, { "toRack", tr }, { "to", ti }, { "copy", copy } }));
        select (tr, fr == tr && ! copy && ti > fs ? ti - 1 : ti);
    };
    strips.onPluginDropped = [this] (const juce::String& id, int tr, int ti)
    {
        send (makeCmd ("addPlugin", { { "rack", tr }, { "index", ti }, { "id", id } }));
    };
    strips.setLibrary (&library);

    dock.onSelectSlot   = [this] (int r, int s) { select (r, s); };
    dock.onPopOut       = [this] (int r, int s) { popOut (r, s); };
    dock.onRemove       = [this] (int r, int s) { send (makeCmd ("removePlugin", { { "rack", r }, { "slot", s } })); };
    dock.onToggleBypass = [this] (int r, int s) { live.setSlotBypassed (r, s, ! live.getSlot (r, s).bypassed); };
    dock.onStepRack     = [this] (int d) { stepRack (d); };
    dock.onPreferredHeightChanged = [this] { resized(); };
    dock.levelFor = [this] (int r, bool out) { return strips.getLevel (r, out); };
    dock.onSlotDropped = strips.onSlotDropped;
    dock.setFullView (false, true);
    dock.setEditorsShown (false);
    {
        juce::Component::SafePointer<ClientView> safe (this);
        dock.createRemoteView = [safe] (int r, int s) -> std::unique_ptr<RemotePluginView>
        {
            if (safe == nullptr || ! (bool) safe->state()["pluginWindows"])
                return nullptr;
            return std::make_unique<RemotePluginView> (safe->live.getHost(), safe->live.getPort(),
                                                       "view=" + juce::String (r) + ":" + juce::String (s));
        };
    }

    hotBar.onOpen = [this] (int id) { openSlotId (id); };
    pageTabs.onAddPage = [this] { addPage(); };
    pageTabs.onPageMenu = [this] (int p) { showPageMenu (p); };

    overlay = std::make_unique<Overlay> (*this);
    addAndMakeVisible (*overlay);

    live.addChangeListener (this);
    addKeyListener (this);
    setWantsKeyboardFocus (true);
    updateViewButtons();
    startTimerHz (30);
}

ClientComponent::ClientView::~ClientView()
{
    stopTimer();
    removeKeyListener (this);
    live.removeChangeListener (this);
    for (auto& d : dialogs)
        if (d != nullptr)
            delete d.getComponent();
    popOuts.clear();
    pickerOverlay.reset();
    dock.releaseEditor();
    tool.reset();
}

//==============================================================================
void ClientComponent::ClientView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (live.isConnected() && live.hasEverConnected())
    {
        rememberServer (live.getServerName(), live.getHost(), live.getPort());

        if (! libraryLoaded)
            loadLibrary();
    }

    // The plugin list changed after a scan on the server
    const bool scanning = live.getScanStatus().running;
    if (scanWasRunning && ! scanning)
        loadLibrary();
    scanWasRunning = scanning;

    // Follow the server's selection when it changes there (a console's channel
    // select, the iPad); clicks here stay here
    const int sr = state().hasProperty ("selected") ? (int) state()["selected"] : -1;
    const int ss = state().hasProperty ("selectedSlot") ? (int) state()["selectedSlot"] : -1;
    if (serverSelectedRack == -2)
    {
        serverSelectedRack = sr;
        serverSelectedSlot = ss;
        if (selectedRack < 0 && sr >= 0)
        {
            selectedRack = sr;
            selectedSlot = ss;
        }
    }
    else if (sr != serverSelectedRack || ss != serverSelectedSlot)
    {
        serverSelectedRack = sr;
        serverSelectedSlot = ss;
        if (sr >= 0)
        {
            selectedRack = sr;
            selectedSlot = ss;
            strips.scrollToRack (sr);
        }
    }

    // A rack added from here: select it
    const int n = live.getNumRacks();
    if (selectNewRack && racksBefore >= 0 && n > racksBefore)
    {
        selectNewRack = false;
        selectedRack = n - 1;
        selectedSlot = -1;
        strips.scrollToRack (selectedRack);
    }
    racksBefore = n;

    refreshAll();
    overlay->setVisible (! live.isConnected());
    updateWindowTitle();
    repaint();
}

void ClientComponent::ClientView::loadLibrary()
{
    libraryLoaded = true;
    juce::Component::SafePointer<ClientView> safe (this);
    live.request ("descriptions", [safe] (const juce::var& data)
    {
        if (safe == nullptr)
            return;
        juce::Array<juce::PluginDescription> types;
        if (auto* list = data["descriptions"].getArray())
            for (auto& x : *list)
                if (auto xml = juce::parseXML (x.toString()))
                {
                    juce::PluginDescription d;
                    if (d.loadFromXml (*xml))
                        types.add (d);
                }
        safe->library.rebuild (types);
        safe->strips.forgetMeters();
        safe->strips.refresh();
    });
}

void ClientComponent::ClientView::timerCallback()
{
    strips.updateMeters();
    dock.updateMeters();
    ++ticks;
    repaint (bpmArea);

    if (ticks % 3 == 0)
        keysBar->repaint();
    if (ticks % 8 == 0)
    {
        statusBar->repaint();
        repaint (sceneChip.expanded (24));
        overlay->setVisible (! live.isConnected());
    }
}

bool ClientComponent::ClientView::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    const auto cmd = juce::ModifierKeys::commandModifier;
    if (key == juce::KeyPress ('s', cmd, 0)) { saveButton.triggerClick(); return true; }
    if (key == juce::KeyPress ('s', cmd | juce::ModifierKeys::shiftModifier, 0)) { saveShowAs(); return true; }
    if (key == juce::KeyPress ('o', cmd, 0)) { showShowMenu(); return true; }
    if (key == juce::KeyPress::escapeKey && view != View::channels)
    {
        setView (view == View::rack ? View::channels : lastRackView);
        return true;
    }
    if (key == juce::KeyPress::F1Key || key == juce::KeyPress::F2Key || key == juce::KeyPress::F3Key || key == juce::KeyPress::F4Key
        || key == juce::KeyPress::F5Key || key == juce::KeyPress::F6Key || key == juce::KeyPress::F7Key || key == juce::KeyPress::F8Key)
    {
        const int k = key.getKeyCode() - juce::KeyPress::F1Key;
        const auto a = keysBar->actionFor (k);
        if (a.type != Action::none)
            performAction (a, -1);
        return true;
    }
    return false;
}

//==============================================================================
void ClientComponent::ClientView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
    g.setColour (Theme::panel);
    g.fillRect (0, 0, getWidth(), toolbarHeight);
    g.setColour (Theme::border);
    g.fillRect (0, toolbarHeight, getWidth(), 1);
    drawLogo (g, toolbarHeight, "CLIENT");

    for (auto& grp : toolbarGroups)
    {
        g.setColour (Theme::well);
        g.fillRoundedRectangle (grp.toFloat(), 8.0f);
        g.setColour (Theme::border);
        g.drawRoundedRectangle (grp.toFloat().reduced (0.5f), 8.0f, 1.0f);
    }

    // Snapshot chip: console light, number, name (click for the list)
    auto chip = sceneChip.toFloat();
    const bool linked = live.getNumConsoles() > 0 && live.getConsole (0).connected;
    Theme::drawLight (g, juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ chip.getX() + 10.0f, chip.getCentreY() }),
                      linked ? Theme::green : Theme::border, linked);

    const int scene = currentScene();
    auto t = chip.withTrimmedLeft (22.0f).reduced (0.0f, 3.0f);
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText (scene >= 0 ? "SNAPSHOT " + juce::String (scene) : juce::String ("SNAPSHOTS"),
                t.removeFromTop (t.getHeight() * 0.42f).toNearestInt(), juce::Justification::bottomLeft);
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    const auto snapshots = live.getSnapshots();
    const bool stored = live.hasSnapshot (scene);
    const auto name = scene < 0 ? juce::String ((int) snapshots.size()) + " in show"
                    : ! stored ? juce::String ("Not stored")
                    : live.getSnapshotName (scene).isNotEmpty() ? live.getSnapshotName (scene) : juce::String ("Untitled");
    g.drawFittedText (name, t.toNearestInt(), juce::Justification::topLeft, 1, 0.8f);

    // Tempo, with a beat light
    if (! bpmArea.isEmpty())
    {
        auto r = bpmArea.toFloat();
        const double beatMs = 60000.0 / bpm();
        const bool onBeat = std::fmod (juce::Time::getMillisecondCounterHiRes(), beatMs) < 90.0;
        Theme::drawLight (g, juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ r.getX() + 14.0f, r.getCentreY() }),
                          onBeat ? Theme::amber : Theme::border, onBeat);
        auto bt = r.withTrimmedLeft (26.0f).reduced (4.0f, 3.0f);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText ("BPM", bt.removeFromTop (12.0f).toNearestInt(), juce::Justification::centredLeft);
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        const double b = bpm();
        g.drawText (juce::String (b, std::abs (b - std::round (b)) < 0.05 ? 0 : 1), bt.toNearestInt(), juce::Justification::centredLeft);
    }
}

void ClientComponent::ClientView::resized()
{
    auto area = getLocalBounds();
    overlay->setBounds (area);

    auto bar = area.removeFromTop (toolbarHeight).reduced (12, 10);
    logoArea = { 0, 0, 150, toolbarHeight };
    bar.removeFromLeft (140);
    toolbarGroups.clear();

    Theme::IconButton* labelled[] = { &saveButton, &loadButton, &overviewButton, &rackViewButton, &devicesButton, &routingButton,
                                      &scanButton, &scopeButton, &addRackButton, &stackRowsButton };
    int labelledWidth = 0;
    for (auto* b : labelled)
        labelledWidth += b->getLabelledWidth() - 40;
    const bool showLabels = bar.getWidth() >= 960 + labelledWidth;
    for (auto* b : labelled)
        b->setShowsLabel (showLabels);
    auto w = [showLabels] (Theme::IconButton& b) { return showLabels ? b.getLabelledWidth() : 40; };

    auto group = [this, &bar] (std::initializer_list<std::pair<juce::Component*, int>> items, bool fromRight = false, juce::Rectangle<int>* spot = nullptr)
    {
        int width = 4;
        for (auto& it : items) width += it.second + 4;
        auto grp = fromRight ? bar.removeFromRight (width) : bar.removeFromLeft (width);
        toolbarGroups.push_back (grp);
        auto inner = grp.reduced (4);
        for (auto& it : items)
        {
            auto cell = inner.removeFromLeft (it.second);
            if (it.first != nullptr)
                it.first->setBounds (cell);
            else if (spot != nullptr)
                *spot = cell;
            inner.removeFromLeft (4);
        }
        if (fromRight) bar.removeFromRight (12); else bar.removeFromLeft (12);
    };

    group ({ { &saveButton, w (saveButton) }, { &loadButton, w (loadButton) } });
    group ({ { &prevSceneButton, 34 }, { nullptr, 168 }, { &nextSceneButton, 34 }, { &storeSceneButton, 64 } }, false, &sceneChip);
    group ({ { &overviewButton, w (overviewButton) }, { &rackViewButton, w (rackViewButton) }, { &devicesButton, w (devicesButton) },
             { &routingButton, w (routingButton) }, { &scanButton, w (scanButton) }, { &scopeButton, w (scopeButton) } });
    group ({ { &addRackButton, w (addRackButton) }, { &stackRowsButton, w (stackRowsButton) } });
    group ({ { nullptr, 84 }, { &tapButton, 60 } }, true, &bpmArea);

    statusBar->setBounds (area.removeFromBottom (30));

    area.reduce (12, 8);
    hotBar.setBounds (area.removeFromTop (HotPluginsBar::preferredHeight));
    area.removeFromTop (6);
    pageTabs.setBounds (area.removeFromTop (PageTabs::preferredHeight));
    area.removeFromTop (2);
    keysBar->setBounds (area.removeFromBottom (KeysBar::preferredHeight));
    area.removeFromBottom (8);

    if (pickerOverlay != nullptr)
        pickerOverlay->setBounds (getLocalBounds());

    const bool toolShown = tool != nullptr;
    hotBar.setVisible (! toolShown);
    pageTabs.setVisible (view == View::channels);
    strips.setVisible (view == View::channels);
    dock.setVisible (view == View::rack);

    if (toolShown)
        tool->setBounds (area.withTop (hotBar.getY()));
    else if (view == View::rack)
        dock.setBounds (area.withTop (pageTabs.getY()));
    else
        strips.setBounds (area);
}

void ClientComponent::ClientView::mouseUp (const juce::MouseEvent& e)
{
    if (sceneChip.contains (e.getPosition()))
        showSnapshotMenu();
    else if (logoArea.contains (e.getPosition()))
        showLogoMenu();
    else if (bpmArea.contains (e.getPosition()))
    {
        auto* w = new juce::AlertWindow ("Tempo", "Beats per minute for tempo-synced plugins.", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("bpm", juce::String (bpm(), 1));
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<ClientView> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int r)
        {
            const double v = w->getTextEditorContents ("bpm").getDoubleValue();
            if (safe != nullptr && r == 1 && v > 0.0)
                safe->send (makeCmd ("bpm", { { "value", v } }));
        }), true);
    }
}

void ClientComponent::ClientView::showLogoMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("LiveRack Client " JUCE_APPLICATION_VERSION_STRING);
    m.addItem (-1, "Connected to " + live.getServerName() + " (" + live.getHost() + ")"
                   + (live.getServerVersion().isNotEmpty() ? ", LiveRack " + live.getServerVersion() : juce::String()), false);
    m.addSeparator();
    m.addItem (1, "Disconnect / choose another server");
    m.addItem (2, "Run shows on this computer (LiveRack Native)");

    auto& o = owner;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (logoArea)), [&o] (int r)
    {
        if (r == 1) juce::MessageManager::callAsync ([&o] { o.showConnectScreen(); });
        if (r == 2 && AppMode::switchTo != nullptr) AppMode::switchTo (false);
    });
}

void ClientComponent::ClientView::updateWindowTitle()
{
    const auto show = state()["show"].toString();
    const auto title = "LiveRack Client - " + (show.isNotEmpty() ? show : juce::String ("Untitled show"))
                     + ((bool) state()["edited"] ? " (edited)" : "") + " on " + live.getServerName();
    if (title != shownTitle)
        if (auto* w = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent()))
        {
            shownTitle = title;
            w->setName (title);
        }
}

//==============================================================================
// Selection and views
void ClientComponent::ClientView::select (int rack, int slot)
{
    if (slot < 0)
        slot = (rack == selectedRack) ? selectedSlot : 0;

    selectedRack = rack;
    selectedSlot = slot;

    // Show it: switch to a page it's on, or All
    if (juce::isPositiveAndBelow (rack, live.getNumRacks()) && ! live.isRackOnPage (rack, live.getCurrentPage()))
    {
        int page = -1;
        for (int p = 0; p < (int) live.getPages().size() && page < 0; ++p)
            if (live.isRackOnPage (rack, p))
                page = p;
        live.setCurrentPage (page);
    }

    refreshAll();
    strips.scrollToRack (selectedRack);
    if (routingMatrix != nullptr)
        routingMatrix->setSelectedRack (rack);
}

void ClientComponent::ClientView::stepRack (int delta)
{
    std::vector<int> order;
    const int page = live.getCurrentPage();
    for (int r = 0; r < live.getNumRacks(); ++r)
        if (page < 0 || live.isRackOnPage (r, page))
            order.push_back (r);
    if (order.empty())
        return;

    const auto it = std::find (order.begin(), order.end(), selectedRack);
    const int at = it == order.end() ? (delta > 0 ? -1 : 0) : (int) (it - order.begin());
    const int n = (int) order.size();
    select (order[(size_t) (((at + delta) % n + n) % n)], -1);
}

void ClientComponent::ClientView::refreshAll()
{
    const int n = live.getNumRacks();
    if (n == 0)
    {
        selectedRack = selectedSlot = -1;
    }
    else
    {
        selectedRack = juce::jlimit (0, n - 1, juce::jmax (0, selectedRack));
        const int slots = live.getNumSlots (selectedRack);
        selectedSlot = slots == 0 ? -1 : juce::jlimit (0, slots - 1, juce::jmax (0, selectedSlot));
    }

    strips.setSelection (selectedRack, selectedSlot);
    strips.refresh();
    hotBar.repaint();
    pageTabs.repaint();
    keysBar->repaint();
    if (routingMatrix != nullptr)  routingMatrix->refresh();
    if (snapshotsPanel != nullptr) snapshotsPanel->refresh();

    if (pickerOverlay == nullptr)
        dock.show (selectedRack, selectedSlot);
}

void ClientComponent::ClientView::openSlotId (int slotId)
{
    int r, s;
    if (! live.findSlot (slotId, r, s))
        return;
    select (r, s);
    setView (View::rack);
}

void ClientComponent::ClientView::setView (View v)
{
    if (v == View::channels || v == View::rack)
        lastRackView = v;

    const bool toolChanged = v != view;
    view = v;

    if (toolChanged)
    {
        tool.reset();
        devicesPanel = nullptr;
        routingMatrix = nullptr;
        snapshotsPanel = nullptr;
        juce::Component::SafePointer<ClientView> safe (this);

        if (v == View::devices)
        {
            DevicesPanel::Host h { live, nullptr, nullptr,
                [safe] (int d)
                {
                    if (safe != nullptr)
                        openDialog (new DriverSettingsPanel (safe->live, d), "Audio driver " + juce::String (d + 1) + " on " + safe->live.getServerName(), safe->dialogs);
                },
                [safe] (int c)
                {
                    if (safe != nullptr)
                        openDialog (new ConsoleLinkPanel (safe->live, c), "Console " + juce::String (c + 1) + " settings", safe->dialogs);
                },
                [] {},
                [] { return 0; } };
            devicesPanel = new DevicesPanel (std::move (h));
            tool.reset (devicesPanel);
        }
        else if (v == View::routing)
        {
            routingMatrix = new RoutingMatrix (live);
            routingMatrix->onSelectRack = [safe] (int r)
            {
                if (safe != nullptr && r != safe->selectedRack)
                {
                    safe->selectedRack = r;
                    safe->selectedSlot = 0;
                    safe->refreshAll();
                }
            };
            routingMatrix->setSelectedRack (selectedRack);
            tool.reset (routingMatrix);
        }
        else if (v == View::plugins)
        {
            tool = std::make_unique<PluginsPanel> (live);
        }
        else if (v == View::snapshots)
        {
            SnapshotsPanel::Host h { live,
                [safe] { return safe != nullptr ? safe->currentScene() : -1; },
                [safe] (int n) { if (safe != nullptr) safe->send (makeCmd ("scene", { { "number", n } })); },
                [safe] (int n, const juce::String& name, const SnapshotStoreFilter& filter)
                {
                    if (safe == nullptr) return;
                    auto c = makeCmd ("sceneStore", { { "number", n }, { "name", name } });
                    if (! filter.everything)
                    {
                        juce::Array<juce::var> slots, racks;
                        for (int id : filter.slotIds)
                        {
                            int r, s;
                            if (safe->live.findSlot (id, r, s))
                                slots.add (juce::Array<juce::var> { r, s });
                        }
                        for (int id : filter.rackIds)
                            if (const int r = safe->live.findRackById (id); r >= 0)
                                racks.add (r);
                        c.getDynamicObject()->setProperty ("slots", slots);
                        c.getDynamicObject()->setProperty ("racks", racks);
                    }
                    safe->send (c);
                },
                [safe] (const juce::String& t, const juce::String& m, const juce::String& ok, std::function<void()> onOk)
                {
                    if (safe != nullptr) safe->confirm (t, m, ok, std::move (onOk));
                },
                [safe] (int n) -> juce::String
                {
                    if (safe == nullptr) return {};
                    juce::StringArray parts;
                    const auto linkedScene = safe->live.getSnapshotConsoleScene (n);
                    for (int i = 0; i < safe->live.getNumConsoles(); ++i)
                    {
                        const auto c = safe->live.getConsole (i);
                        if (c.settings.type != ConsoleLink::Type::off && c.settings.recallScenes)
                            parts.add (c.settings.name + " scene " + (linkedScene.isNotEmpty() ? linkedScene : juce::String (n)));
                    }
                    return parts.joinIntoString (", ");
                },
                [] (const juce::String&) {},
                [safe]
                {
                    if (safe == nullptr || safe->live.getNumConsoles() == 0) return juce::String();
                    const auto c = safe->live.getConsole (0);
                    return c.currentScene >= 0 ? juce::String (c.currentScene) : juce::String();
                } };
            snapshotsPanel = new SnapshotsPanel (std::move (h));
            tool.reset (snapshotsPanel);
        }

        if (tool != nullptr)
            addAndMakeVisible (*tool);
    }

    // The plugin picture only streams while Rack shows it
    dock.setFullView (view == View::rack, true);
    dock.setEditorsShown (view == View::rack);
    updateViewButtons();
    resized();
    refreshAll();

    if (overlay != nullptr)
        overlay->toFront (false);
}

void ClientComponent::ClientView::updateViewButtons()
{
    overviewButton.setToggleState (view == View::channels, juce::dontSendNotification);
    rackViewButton.setToggleState (view == View::rack, juce::dontSendNotification);
    devicesButton.setToggleState (view == View::devices, juce::dontSendNotification);
    routingButton.setToggleState (view == View::routing, juce::dontSendNotification);
    scanButton.setToggleState (view == View::plugins, juce::dontSendNotification);
    scopeButton.setToggleState (view == View::snapshots, juce::dontSendNotification);
}

//==============================================================================
// Actions (User Keys, Hot Plugins), against this window's selection
void ClientComponent::ClientView::performAction (const Action& a, int value)
{
    auto slotOf = [this] (int slotId, int& r, int& s)
    {
        if (slotId <= 0) { r = selectedRack; s = selectedSlot; return live.getSlot (r, s).exists; }
        return live.findSlot (slotId, r, s);
    };

    switch (a.type)
    {
        case Action::rackBypass:
        {
            const int r = a.rack < 0 ? selectedRack : a.rack;
            if (juce::isPositiveAndBelow (r, live.getNumRacks()))
                live.setRackBypassed (r, value < 0 ? ! live.isRackBypassed (r) : value > 0);
            break;
        }
        case Action::pluginBypass:
        {
            int r, s;
            if (slotOf (a.slotId, r, s))
                live.setSlotBypassed (r, s, value < 0 ? ! live.getSlot (r, s).bypassed : value > 0);
            break;
        }
        case Action::selectRack:
            if (juce::isPositiveAndBelow (a.rack, live.getNumRacks()))
                select (a.rack, -1);
            break;
        case Action::nextRack:     stepRack (1); break;
        case Action::prevRack:     stepRack (-1); break;
        case Action::openPlugin:   openSlotId (a.slotId); break;
        case Action::hotPlugin:
        {
            const auto hot = live.getHotPlugins();
            if (juce::isPositiveAndBelow (a.number - 1, (int) hot.size()))
                openSlotId (hot[(size_t) a.number - 1]);
            break;
        }
        case Action::recallScene:  send (makeCmd ("scene", { { "number", a.number } })); break;
        case Action::nextScene:    send (makeCmd ("sceneStep", { { "dir", 1 } })); break;
        case Action::prevScene:    send (makeCmd ("sceneStep", { { "dir", -1 } })); break;
        case Action::tapTempo:     send (makeCmd ("tap")); break;
        case Action::fullView:     setView ((value < 0 ? view != View::rack : value > 0) ? View::rack : View::channels); break;
        case Action::storeScene:   send (makeCmd ("sceneStore", { { "number", juce::jmax (0, currentScene()) } })); break;
        case Action::showPage:     live.setCurrentPage (a.number - 1); break;
        case Action::twoWindows: case Action::none: case Action::numTypes: default: break;
    }

    keysBar->repaint();
    hotBar.repaint();
}

bool ClientComponent::ClientView::isActionActive (const Action& a) const
{
    auto& l = const_cast<RemoteConnection&> (live);
    switch (a.type)
    {
        case Action::rackBypass:   return l.isRackBypassed (a.rack < 0 ? selectedRack : a.rack);
        case Action::pluginBypass:
        {
            int r = selectedRack, s = selectedSlot;
            if (a.slotId > 0 && ! l.findSlot (a.slotId, r, s))
                return false;
            return l.getSlot (r, s).bypassed;
        }
        case Action::selectRack:   return a.rack == selectedRack;
        case Action::openPlugin:
        {
            int r, s;
            return l.findSlot (a.slotId, r, s) && r == selectedRack && s == selectedSlot;
        }
        case Action::hotPlugin:
        {
            const auto hot = l.getHotPlugins();
            int r, s;
            return juce::isPositiveAndBelow (a.number - 1, (int) hot.size())
                   && l.findSlot (hot[(size_t) a.number - 1], r, s) && r == selectedRack && s == selectedSlot;
        }
        case Action::recallScene:  return currentScene() == a.number;
        case Action::fullView:     return view == View::rack;
        case Action::showPage:     return l.getCurrentPage() == a.number - 1;
        case Action::tapTempo:     return std::fmod (juce::Time::getMillisecondCounterHiRes(), 60000.0 / bpm()) < 100.0;
        case Action::twoWindows: case Action::nextScene: case Action::prevScene: case Action::storeScene:
        case Action::nextRack: case Action::prevRack: case Action::none: case Action::numTypes: default:
            return false;
    }
}

//==============================================================================
// Racks
void ClientComponent::ClientView::showRackMenu (int rack)
{
    if (! juce::isPositiveAndBelow (rack, live.getNumRacks()))
        return;
    const auto cfg = live.getRackConfig (rack);
    const auto pages = live.getPages();

    juce::PopupMenu m;
    m.addSectionHeader (cfg.name);
    m.addItem (1, "Name and console channel...");
    m.addItem (5, "Routing...");
    m.addItem (2, "Bypass", true, live.isRackBypassed (rack));
    m.addItem (3, "Recall safe", true, cfg.recallSafe);

    juce::PopupMenu groups;
    groups.addItem (200, "None", true, cfg.latencyGroup == 0);
    for (int g = 1; g <= 8; ++g)
        groups.addColouredItem (200 + g, "Group " + RackConfig::groupName (g), Theme::groupColour (g), true, cfg.latencyGroup == g);
    m.addSubMenu ("Latency group", groups);

    juce::PopupMenu pagesMenu;
    for (int p = 0; p < (int) pages.size(); ++p)
        pagesMenu.addItem (300 + p, pages[(size_t) p].name, true, live.isRackOnPage (rack, p));
    if (! pages.empty())
        pagesMenu.addSeparator();
    pagesMenu.addItem (299, "New page with this rack...");
    m.addSubMenu ("Pages", pagesMenu);

    juce::PopupMenu colours;
    colours.addItem (99, "Automatic", true, cfg.colourIndex < 0);
    for (int i = 0; i < RackStripsView::numColours; ++i)
        colours.addColouredItem (100 + i, RackStripsView::colourName (i), RackStripsView::colourForIndex (i), true, cfg.colourIndex == i);
    m.addSubMenu ("Colour", colours);

    m.addSeparator();
    m.addItem (4, "Remove rack");

    juce::Component::SafePointer<ClientView> safe (this);
    const int rackId = cfg.id;
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, rack, rackId] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& l = safe->live;
        if (l.findRackById (rackId) != rack)
            return;   // the racks changed meanwhile

        switch (result)
        {
            case 1: safe->showRackDialog (rack); break;
            case 2: l.setRackBypassed (rack, ! l.isRackBypassed (rack)); break;
            case 3: l.setRackRecallSafe (rack, ! l.getRackConfig (rack).recallSafe); break;
            case 4:
                safe->confirm ("Remove " + l.getRackConfig (rack).name + "?", "Its plugins and their settings go with it.", "Remove",
                               [safe, rack] { if (safe != nullptr) safe->send (makeCmd ("removeRack", { { "rack", rack } })); });
                break;
            case 5: safe->select (rack, -1); safe->setView (View::routing); break;
            case 99: l.setRackColour (rack, -1); break;
            default:
                if (result >= 100 && result < 100 + RackStripsView::numColours)
                    l.setRackColour (rack, result - 100);
                if (result >= 200 && result <= 208)
                    l.setRackLatencyGroup (rack, result - 200);
                if (result == 299)
                {
                    safe->selectedRack = rack;
                    safe->addPage();
                }
                if (result >= 300)
                {
                    auto ps = l.getPages();
                    const int p = result - 300;
                    if (juce::isPositiveAndBelow (p, (int) ps.size()))
                    {
                        auto& ids = ps[(size_t) p].rackIds;
                        const auto it = std::find (ids.begin(), ids.end(), rackId);
                        if (it != ids.end()) ids.erase (it);
                        else                 ids.push_back (rackId);
                        l.setPages (ps);
                    }
                }
                break;
        }
    });
}

void ClientComponent::ClientView::showRackDialog (int rack)
{
    if (! juce::isPositiveAndBelow (rack, live.getNumRacks()))
        return;
    const auto cfg = live.getRackConfig (rack);

    auto* w = new juce::AlertWindow ("Rack", "Routing is in Routing; format, latency group, Recall Safe and colour are the badges on the strip.",
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", cfg.name, "Name");
    w->addTextEditor ("console", juce::String (cfg.consoleChannel), "Console channel (0 = none)");
    if (live.getNumConsoles() > 1)
    {
        juce::StringArray names;
        for (int i = 0; i < live.getNumConsoles(); ++i)
            names.add (live.getConsole (i).displayName);
        w->addComboBox ("whichConsole", names, "On console");
        w->getComboBoxComponent ("whichConsole")->setSelectedItemIndex (juce::jlimit (0, names.size() - 1, cfg.console));
    }
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<ClientView> safe (this);
    const int rackId = cfg.id;
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, rackId] (int result)
    {
        if (safe == nullptr || result != 1)
            return;
        const int r = safe->live.findRackById (rackId);
        if (r < 0)
            return;
        auto c = makeCmd ("rackConfig", { { "rack", r }, { "name", w->getTextEditorContents ("name") },
                                          { "console", juce::jmax (0, w->getTextEditorContents ("console").getIntValue()) } });
        if (auto* box = w->getComboBoxComponent ("whichConsole"))
            c.getDynamicObject()->setProperty ("consoleIndex", juce::jmax (0, box->getSelectedItemIndex()));
        safe->send (c);
    }), true);
}

void ClientComponent::ClientView::changeFormat (int rack, const RackConfig& cfg)
{
    const auto format = cfg.isMonoToStereo() ? "m2s" : cfg.stereo ? "stereo" : "mono";
    auto c = makeCmd ("rackConfig", { { "rack", rack }, { "format", format } });

    if (live.getNumSlots (rack) == 0)
    {
        send (c);
        return;
    }

    confirm (cfg.stereo ? "Make this rack stereo?" : "Make this rack mono?",
             "Its plugins are switched over and re-prepared, so this rack drops out for a moment.",
             cfg.stereo ? "Make stereo" : "Make mono",
             [this, c] { send (c); });
}

void ClientComponent::ClientView::addPage()
{
    auto* w = new juce::AlertWindow ("New page", "A page shows just the racks you put on it.", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", "Page " + juce::String ((int) live.getPages().size() + 1), "Name");
    if (juce::isPositiveAndBelow (selectedRack, live.getNumRacks()))
        w->addComboBox ("start", { "Start with " + live.getRackConfig (selectedRack).name, "Start empty" }, "Racks");
    w->addButton ("Add", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<ClientView> safe (this);
    const int rackId = juce::isPositiveAndBelow (selectedRack, live.getNumRacks()) ? live.getRackConfig (selectedRack).id : -1;
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, rackId] (int r)
    {
        if (safe == nullptr || r != 1)
            return;
        auto pages = safe->live.getPages();
        RackPage p;
        p.name = w->getTextEditorContents ("name").trim();
        if (p.name.isEmpty()) p.name = "Page";
        auto* start = w->getComboBoxComponent ("start");
        if (start != nullptr && start->getSelectedItemIndex() == 0 && rackId >= 0)
            p.rackIds.push_back (rackId);
        pages.push_back (p);
        safe->live.setPages (pages);
        safe->live.setCurrentPage ((int) pages.size() - 1);
    }), true);
}

void ClientComponent::ClientView::showPageMenu (int page)
{
    const auto pages = live.getPages();
    if (! juce::isPositiveAndBelow (page, (int) pages.size()))
        return;

    const int count = (int) pages.size();
    juce::PopupMenu m;
    m.addSectionHeader (pages[(size_t) page].name);
    m.addItem (1, "Rename...");
    if (juce::isPositiveAndBelow (selectedRack, live.getNumRacks()))
        m.addItem (2, "Add " + live.getRackConfig (selectedRack).name + " to this page", ! live.isRackOnPage (selectedRack, page));
    m.addItem (3, "Move left", page > 0);
    m.addItem (4, "Move right", page < count - 1);
    m.addSeparator();
    m.addItem (5, "Delete page (racks stay)");

    juce::Component::SafePointer<ClientView> safe (this);
    const int rackId = juce::isPositiveAndBelow (selectedRack, live.getNumRacks()) ? live.getRackConfig (selectedRack).id : -1;
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, page, rackId] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& l = safe->live;
        auto ps = l.getPages();
        if (! juce::isPositiveAndBelow (page, (int) ps.size()))
            return;

        if (r == 1)
        {
            auto* w = new juce::AlertWindow ("Rename page", {}, juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", ps[(size_t) page].name);
            w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, page, w] (int res)
            {
                if (safe == nullptr || res != 1) return;
                auto list = safe->live.getPages();
                if (juce::isPositiveAndBelow (page, (int) list.size()) && w->getTextEditorContents ("name").trim().isNotEmpty())
                {
                    list[(size_t) page].name = w->getTextEditorContents ("name").trim();
                    safe->live.setPages (list);
                }
            }), true);
        }
        if (r == 2 && rackId >= 0)
        {
            ps[(size_t) page].rackIds.push_back (rackId);
            l.setPages (ps);
        }
        if (r == 3 || r == 4)
        {
            const int other = r == 3 ? page - 1 : page + 1;
            std::swap (ps[(size_t) page], ps[(size_t) other]);
            l.setPages (ps);
            l.setCurrentPage (other);
        }
        if (r == 5)
        {
            ps.erase (ps.begin() + page);
            l.setPages (ps);
            l.setCurrentPage (-1);
        }
    });
}

//==============================================================================
// Plugins
void ClientComponent::ClientView::showAddPluginMenu (int rack)
{
    if (library.getFamilies().empty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "No plugins yet",
                                                live.getNumRacks() > 0 && ! libraryLoaded ? "The plugin list is still on its way from the server."
                                                                                          : "Use Plugins to scan for plugins on the server.");
        return;
    }

    const auto cfg = live.getRackConfig (rack);
    const int end = live.getNumSlots (rack);
    const int point = live.getStereoPoint (rack);
    juce::String rackText = cfg.stereo ? "stereo rack" : "mono rack";
    if (cfg.isMonoToStereo())
        rackText = point >= 0 ? "mono-to-stereo rack, stereo from here" : "mono-to-stereo rack, still mono";

    juce::Component::SafePointer<ClientView> safe (this);
    const int rackId = cfg.id;

    auto picker = std::make_unique<PluginPicker> (library,
        [cfg, point, end] (const PluginLibrary::Family& f)
        {
            if (cfg.stereo)              return PluginLibrary::Want::stereo;
            if (! cfg.isMonoToStereo())  return PluginLibrary::Want::mono;
            if (point >= 0)              return end > point ? PluginLibrary::Want::stereo : PluginLibrary::Want::mono;
            return PluginLibrary::suitsStereoPoint (f.category) ? PluginLibrary::Want::monoToStereo : PluginLibrary::Want::mono;
        },
        rackText,
        [safe, rackId] (const juce::PluginDescription& desc)
        {
            if (safe == nullptr)
                return;
            const int r = safe->live.findRackById (rackId);
            if (r < 0)
                return;
            // The server picks the mono / stereo version for the rack
            const auto key = PluginLibrary::familyKey (desc);
            safe->library.noteUsed (key);
            safe->send (makeCmd ("addPlugin", { { "rack", r }, { "key", key } }));
            safe->selectedRack = r;
            safe->selectedSlot = safe->live.getNumSlots (r);   // the new one, once it's there
        });

    auto close = [safe]
    {
        juce::MessageManager::callAsync ([safe]
        {
            if (safe == nullptr) return;
            safe->pickerOverlay.reset();
            safe->refreshAll();
        });
    };
    picker->onClose = close;
    pickerOverlay = std::make_unique<PickerOverlay> (std::move (picker), close);
    addAndMakeVisible (*pickerOverlay);
    pickerOverlay->setBounds (getLocalBounds());
}

void ClientComponent::ClientView::showSlotMenu (int rack, int slot)
{
    const auto s = live.getSlot (rack, slot);
    if (! s.exists)
        return;

    const int numSlots = live.getNumSlots (rack);
    const auto hotList = live.getHotPlugins();
    const bool hot = std::find (hotList.begin(), hotList.end(), s.id) != hotList.end();

    juce::PopupMenu m;
    m.addSectionHeader (s.name);
    m.addItem (1, "Bypass", true, s.bypassed);
    m.addItem (2, "Open in its own window", (bool) state()["pluginWindows"]);
    m.addItem (8, hot ? "Unpin from Hot Plugins" : "Pin to Hot Plugins", hot || (int) hotList.size() < RackEngine::maxHotPlugins);
    if (live.getRackConfig (rack).isMonoToStereo())
        m.addItem (10, "Make this the stereo point", true, live.getStereoPoint (rack) == slot);
    m.addItem (3, "Move up", slot > 0);
    m.addItem (4, "Move down", slot < numSlots - 1);
    m.addSeparator();
    m.addItem (12, "Run in its own process (experimental)", true, s.isolated);
    m.addSeparator();
    m.addItem (5, "Remove");

    juce::Component::SafePointer<ClientView> safe (this);
    const int slotId = s.id;
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, slotId, hot] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        int r, sl;
        if (! safe->live.findSlot (slotId, r, sl))
            return;   // gone meanwhile

        switch (result)
        {
            case 1: safe->live.setSlotBypassed (r, sl, ! safe->live.getSlot (r, sl).bypassed); break;
            case 2: safe->popOut (r, sl); break;
            case 3: safe->send (makeCmd ("movePlugin", { { "rack", r }, { "slot", sl }, { "to", sl - 1 } })); safe->select (r, sl - 1); break;
            case 4: safe->send (makeCmd ("movePlugin", { { "rack", r }, { "slot", sl }, { "to", sl + 1 } })); safe->select (r, sl + 1); break;
            case 5: safe->send (makeCmd ("removePlugin", { { "rack", r }, { "slot", sl } })); break;
            case 8: safe->send (makeCmd ("hot", { { "rack", r }, { "slot", sl }, { "on", ! hot } })); break;
            case 10: safe->send (makeCmd ("stereoPoint", { { "rack", r }, { "slot", sl } })); break;
            case 12: safe->send (makeCmd ("isolate", { { "rack", r }, { "slot", sl } })); break;
            default: break;
        }
    });
}

void ClientComponent::ClientView::popOut (int rack, int slot)
{
    const auto s = live.getSlot (rack, slot);
    if (! s.exists)
        return;

    const auto what = "view=" + juce::String (rack) + ":" + juce::String (slot);
    for (auto& w : popOuts)
        if (w->what == what)
        {
            w->toFront (true);
            return;
        }

    // Closing the window removes it
    juce::Component::SafePointer<ClientView> safe (this);
    popOuts.push_back (std::make_unique<RemotePluginWindow> (live.getRackConfig (rack).name + ": " + s.name, live.getHost(), live.getPort(), what,
        [safe, what]
        {
            juce::MessageManager::callAsync ([safe, what]
            {
                if (safe == nullptr) return;
                auto& list = safe->popOuts;
                list.erase (std::remove_if (list.begin(), list.end(), [&what] (const auto& w) { return w->what == what; }), list.end());
            });
        }));
}

//==============================================================================
// Shows (on the server)
void ClientComponent::ClientView::showShowMenu()
{
    juce::Component::SafePointer<ClientView> safe (this);
    live.request ("shows", [safe] (const juce::var& data)
    {
        if (safe == nullptr)
            return;

        juce::PopupMenu m;
        const auto show = safe->state()["show"].toString();
        m.addSectionHeader ((show.isNotEmpty() ? show : juce::String ("Untitled show")) + " on " + safe->live.getServerName());
        m.addItem (1, "Save");
        m.addItem (2, "Save As...");

        juce::PopupMenu open;
        if (auto* list = data["shows"].getArray())
            for (int i = 0; i < list->size(); ++i)
                open.addItem (1000 + i, (*list)[i]["name"].toString(), true, (bool) (*list)[i]["current"]);
        m.addSubMenu ("Open", open, open.getNumItems() > 0);

        juce::PopupMenu recent;
        if (auto* list = data["recent"].getArray())
            for (int i = 0; i < list->size(); ++i)
                recent.addItem (2000 + i, (*list)[i]["name"].toString() + "   " + (*list)[i]["folder"].toString(), true, (bool) (*list)[i]["current"]);
        m.addSubMenu ("Open Recent", recent, recent.getNumItems() > 0);

        m.addSeparator();
        m.addItem (4, "New Show");
        m.addSeparator();
        m.addItem (-1, "Shows are kept on the server, in " + data["showsFolder"].toString(), false);

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&safe->loadButton), [safe, data] (int r)
        {
            if (safe == nullptr || r == 0) return;
            if (r == 1) safe->saveButton.triggerClick();
            if (r == 2) safe->saveShowAs();
            if (r == 4)
                safe->openOrNew ([safe] (bool save) { if (safe != nullptr) safe->send (makeCmd ("showNew", { { "save", save } })); }, "starting a new show");
            if (r >= 1000 && r < 2000)
            {
                const auto entry = data["shows"][r - 1000];
                safe->openOrNew ([safe, entry] (bool save)
                {
                    if (safe != nullptr)
                        safe->send (makeCmd ("showOpen", { { "folder", entry["folder"] }, { "name", entry["name"] }, { "save", save } }));
                }, "opening " + entry["name"].toString());
            }
            if (r >= 2000)
            {
                const int index = (int) data["recent"][r - 2000]["index"];
                safe->openOrNew ([safe, index] (bool save)
                {
                    if (safe != nullptr)
                        safe->send (makeCmd ("showOpenRecent", { { "index", index }, { "save", save } }));
                }, "opening another show");
            }
        });
    });
}

// Unsaved changes: save them first (if the show has a file), or not
void ClientComponent::ClientView::openOrNew (std::function<void (bool saveFirst)> go, const juce::String& what)
{
    if (! (bool) state()["edited"])
    {
        go (false);
        return;
    }

    const bool hasFile = (bool) state()["hasShowFile"];
    auto options = hasFile
        ? juce::MessageBoxOptions::makeOptionsYesNoCancel (juce::MessageBoxIconType::QuestionIcon, "Save the show first?",
                                                          "The show on the server has changes. Save them before " + what + "?",
                                                          "Save", "Don't Save", "Cancel", this)
        : juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::WarningIcon, "Lose the changes?",
                                                        "The show on the server has never been saved. Its changes are lost when " + what + ".",
                                                        "Continue", "Cancel", this);
    juce::AlertWindow::showAsync (options, [go, hasFile] (int result)
    {
        if (hasFile && result == 1) go (true);
        if (hasFile && result == 2) go (false);
        if (! hasFile && result == 1) go (false);
    });
}

void ClientComponent::ClientView::saveShowAs()
{
    auto* w = new juce::AlertWindow ("Save show as", "Saved on " + live.getServerName() + ", in its LiveRack Shows folder.",
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", state()["show"].toString(), "Name");
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<ClientView> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int r)
    {
        const auto name = w->getTextEditorContents ("name").trim();
        if (safe != nullptr && r == 1 && name.isNotEmpty())
            safe->send (makeCmd ("showSave", { { "name", name } }));
    }), true);
}

//==============================================================================
// Snapshots
void ClientComponent::ClientView::showSnapshotMenu()
{
    juce::PopupMenu m;
    const auto list = live.getSnapshots();
    const int scene = currentScene();

    if (list.empty())
        m.addItem (-1, "No snapshots in this show yet", false);
    for (auto& s : list)
        m.addItem (1000 + s.number, juce::String (s.number) + (s.name.isNotEmpty() ? "   " + s.name : juce::String()), true, s.number == scene);

    m.addSeparator();
    m.addItem (1, "Store Snapshot...");
    const bool has = live.hasSnapshot (scene);
    const auto num = has ? " " + juce::String (scene) : juce::String();
    m.addItem (2, "Rename Snapshot" + num + "...", has);
    m.addItem (3, "Delete Snapshot" + num, has);
    m.addItem (4, juce::String::fromUTF8 ("Snapshots" "\xe2\x80\xa6"));

    juce::Component::SafePointer<ClientView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (sceneChip)), [safe, scene] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r >= 1000) { safe->send (makeCmd ("scene", { { "number", r - 1000 } })); return; }
        if (r == 1) safe->showStoreDialog();
        if (r == 4)
        {
            safe->setView (View::snapshots);
            if (safe->snapshotsPanel != nullptr)
                safe->snapshotsPanel->select (scene);
        }
        if (r == 3)
            safe->confirm ("Delete snapshot " + juce::String (scene) + "?", "Its plugin settings are removed from the show.", "Delete",
                           [safe, scene] { if (safe != nullptr) safe->live.deleteSnapshot (scene); });
        if (r == 2)
        {
            auto* w = new juce::AlertWindow ("Rename Snapshot " + juce::String (scene), {}, juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", safe->live.getSnapshotName (scene), "Name");
            w->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, scene] (int res)
            {
                if (safe != nullptr && res == 1)
                    safe->live.renameSnapshot (scene, w->getTextEditorContents ("name"));
            }), true);
        }
    });
}

void ClientComponent::ClientView::showStoreDialog()
{
    const int consoleScene = live.getNumConsoles() > 0 ? live.getConsole (0).currentScene : -1;
    const int suggested = juce::jmax (0, consoleScene >= 0 ? consoleScene : currentScene());

    auto* w = new juce::AlertWindow ("Store Snapshot",
        "Stores the current plugin settings and bypass in the show. When the console recalls this scene "
        "number, LiveRack recalls them. Racks and plugins stay as they are.", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("scene", juce::String (suggested), "Scene number");
    w->addTextEditor ("name", live.getSnapshotName (suggested), "Name (optional)");
    w->addButton ("Store", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<ClientView> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (safe == nullptr || result != 1)
            return;
        const int number = juce::jmax (0, w->getTextEditorContents ("scene").getIntValue());
        const auto name = w->getTextEditorContents ("name");
        auto store = [safe, number, name] { if (safe != nullptr) safe->send (makeCmd ("sceneStore", { { "number", number }, { "name", name } })); };

        if (safe->live.hasSnapshot (number))
            safe->confirm ("Replace snapshot " + juce::String (number) + "?",
                           "The snapshot already exists. Storing replaces its plugin settings with the current ones.", "Replace", store);
        else
            store();
    }), true);
}

void ClientComponent::ClientView::confirm (const juce::String& title, const juce::String& message,
                                           const juce::String& ok, std::function<void()> onOk)
{
    auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, title, message, ok, "Cancel", this);
    juce::AlertWindow::showAsync (options, [onOk] (int result) { if (result == 1 && onOk != nullptr) onOk(); });
}

//==============================================================================
#if LIVERACK_PREVIEW_HOOKS
 #include "ClientPreview.h"
#endif

ClientComponent::ClientComponent (const juce::String& host, int port)
{
    setSize (1440, 940);
    if (host.isNotEmpty())
        connectTo (host, port > 0 ? port : defaultPort, {});
    else
        showConnectScreen();

   #if LIVERACK_PREVIEW_HOOKS
    juce::Timer::callAfterDelay (300, [this] { ClientPreview::run (*this); });
   #endif
}

ClientComponent::~ClientComponent()
{
    view.reset();
    connectScreen.reset();
    finder.reset();
}

void ClientComponent::connectTo (const juce::String& host, int port, const juce::String& name)
{
    connectScreen.reset();
    finder.reset();
    view = std::make_unique<ClientView> (*this, host, port, name);
    addAndMakeVisible (*view);
    view->setBounds (getLocalBounds());
    view->grabKeyboardFocus();
    AppSettings::set ("clientLastServer", host + ":" + juce::String (port));

    if (auto* w = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent()))
        w->setName ("LiveRack Client - " + (name.isNotEmpty() ? name : host));
}

void ClientComponent::showConnectScreen()
{
    view.reset();
    if (finder == nullptr)
        finder = std::make_unique<ServerFinder>();
    connectScreen = std::make_unique<ConnectScreen> (*this, *finder);
    addAndMakeVisible (*connectScreen);
    connectScreen->setBounds (getLocalBounds());

    if (auto* w = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent()))
        w->setName ("LiveRack Client");
}

void ClientComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
}

void ClientComponent::resized()
{
    if (view != nullptr)          view->setBounds (getLocalBounds());
    if (connectScreen != nullptr) connectScreen->setBounds (getLocalBounds());
}
