#pragma once
#include "IsolatedPlugin.h"
#include "ServerMode.h"
#include "SocketUtil.h"
#include "ServerCheck.h"
#include "RemoteConnection.h"
#include "DriverSettingsPanel.h"
#include "DevicesPanel.h"
#include "PluginsPanel.h"
#include "ConsoleLinkPanel.h"
#include "RoutingMatrix.h"
#include "SnapshotsPanel.h"
// Only compiled into Linux preview builds (LIVERACK_PREVIEW_HOOKS=1), used to
// set up a sample session, feed a test signal through it, and open dialogs
// for UI screenshots. Never part of the Mac app.
#include "MainComponent.h"
#include "AppMode.h"

struct PreviewHooks
{
    // Feeds test tones through the racks, standing in for an audio device
    struct TestSignal : public juce::Timer
    {
        explicit TestSignal (RackEngine& e) : engine (e)
        {
            in.setSize (16, block);
            out.setSize (16, block);
            startTimer (10);
        }

        void timerCallback() override
        {
            for (int c = 0; c < in.getNumChannels(); ++c)
            {
                auto* d = in.getWritePointer (c);
                const double amp = 0.08 + 0.42 * (0.5 + 0.5 * std::sin (time * (0.6 + 0.13 * c) + c));
                const double freq = 110.0 * (1 + c % 5);

                for (int i = 0; i < block; ++i)
                    d[i] = (float) (amp * std::sin (juce::MathConstants<double>::twoPi * freq * (sample + i) / 48000.0));
            }

            sample += block;
            time += block / 48000.0;
            PreviewHooks::pump (engine, in, out);
        }

        RackEngine& engine;
        static constexpr int block = 480;
        juce::AudioBuffer<float> in, out;
        juce::int64 sample = 0;
        double time = 0.0;
    };

    static inline std::unique_ptr<TestSignal> testSignal;

    // The rack strips and plugin panel in a second window, reaching this same
    // LiveRack over the network (/live): what LiveRack Client will show
    struct RemoteView : public juce::Component, private juce::ChangeListener, private juce::Timer
    {
        explicit RemoteView (int port) : live ("127.0.0.1", port)
        {
            addAndMakeVisible (strips);
            addAndMakeVisible (dock);
            live.addChangeListener (this);
            strips.onSelect = [this] (int r, int s) { rack = r; slot = s; strips.setSelection (r, s); dock.show (r, juce::jmax (0, s)); };
            dock.onSelectSlot = strips.onSelect;
            dock.levelFor = [this] (int r, bool out) { return strips.getLevel (r, out); };
            startTimerHz (30);
        }
        ~RemoteView() override { live.removeChangeListener (this); }

        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            strips.refresh();
            strips.setSelection (rack, slot);
            dock.show (rack, slot);
        }
        void timerCallback() override { strips.updateMeters(); dock.updateMeters(); }
        void resized() override
        {
            auto b = getLocalBounds().reduced (8);
            strips.setBounds (b.removeFromTop (b.getHeight() / 2));
            b.removeFromTop (8);
            dock.setBounds (b);
        }
        void paint (juce::Graphics& g) override { g.fillAll (Theme::background); }

        RemoteConnection live;
        RackStripsView strips { live };
        PluginDock dock { live };
        int rack = 0, slot = 0;
    };

    struct RemoteWindow : public juce::DocumentWindow
    {
        explicit RemoteWindow (int port) : juce::DocumentWindow ("LiveRack Client (test)", Theme::background, 0)
        {
            setUsingNativeTitleBar (true);
            view = new RemoteView (port);
            setContentOwned (view, false);
            setBounds (0, 0, 1440, 960);
            setVisible (true);
        }
        RemoteView* view = nullptr;
    };

    static inline std::unique_ptr<RemoteWindow> remoteWindow;

    static void openRemoteWindow (MainComponent& m)
    {
        remoteWindow = std::make_unique<RemoteWindow> (m.webRemote.getPort());
        auto& live = remoteWindow->view->live;
        auto& e = m.engine;

        // Compare what the remote copy says with the engine, then change things through it
        juce::Timer::callAfterDelay (3000, [&m, &live, &e]
        {
            std::cerr << "REMOTE connected " << live.isConnected() << " server " << live.getServerName() << std::endl;
            bool same = live.getNumRacks() == e.getNumRacks() && live.getVisibleRacks() == e.getVisibleRacks();
            for (int r = 0; r < e.getNumRacks(); ++r)
            {
                const auto a = live.getRackConfig (r), b = e.getRackConfig (r);
                same = same && a.name == b.name && a.stereo == b.stereo && a.monoToStereo == b.monoToStereo && a.driver == b.driver
                       && a.input[0] == b.input[0] && a.input[1] == b.input[1] && a.output[0] == b.output[0] && a.output[1] == b.output[1]
                       && a.consoleChannel == b.consoleChannel && a.recallSafe == b.recallSafe && a.colourIndex == b.colourIndex
                       && a.latencyGroup == b.latencyGroup && live.isRackBypassed (r) == e.isRackBypassed (r)
                       && live.getNumSlots (r) == e.getNumSlots (r) && live.getStereoPoint (r) == e.getStereoPoint (r)
                       && std::abs (live.getRackTotalLatencyMs (r) - e.getRackTotalLatencyMs (r)) < 0.01;
                for (int s = 0; s < e.getNumSlots (r); ++s)
                    same = same && live.getSlot (r, s).name == e.getPlugin (r, s)->getName() && live.getSlot (r, s).bypassed == e.isSlotBypassed (r, s)
                           && live.getSlot (r, s).key == PluginLibrary::familyKey (e.getSlotDescription (r, s));
                if (! same) { std::cerr << "REMOTE differs at rack " << r << std::endl; break; }
            }
            for (int g = 1; g <= 8; ++g)
                same = same && std::abs (live.getGroupLatencyMs (g) - e.getGroupLatencyMs (g)) < 0.01;
            std::cerr << "REMOTE racks " << live.getNumRacks() << " same as the engine " << same << std::endl;
            std::cerr << "REMOTE meters: rack 0 in " << live.getMeter (0, false) << " out " << live.getMeter (0, true)
                      << " load " << live.getRackLoad (0) << " gr slot0 " << live.getGainReduction (0, 0) << std::endl;

            live.setRackBypassed (1, true);
            live.setRackRecallSafe (2, true);
            live.setRackLatencyGroup (3, 2);
            live.setRackColour (4, 6);
            live.setRackName (5, "Renamed remotely");
            live.setSlotBypassed (0, 0, true);
            m.select (0, 0);

            // A plugin copied to the last rack, as a drag with Option would
            auto* drop = new juce::DynamicObject();
            drop->setProperty ("cmd", "slotDrop");
            drop->setProperty ("rack", 0); drop->setProperty ("slot", 1);
            drop->setProperty ("toRack", 6); drop->setProperty ("to", 0); drop->setProperty ("copy", true);
            std::cerr << "REMOTE last rack before the copy: " << e.getNumSlots (6) << " plugins" << std::endl;
            live.sendCommand (juce::var (drop));
        });

        juce::Timer::callAfterDelay (4500, [&live, &e, &m]
        {
            auto& rs = remoteWindow->view->strips;
            std::cerr << "REMOTE GR snare: server " << live.getGainReduction (2, 0) << " / " << live.getGainReduction (2, 1)
                      << " shown remotely " << rs.getSlotGainReduction (2, 0) << " / " << rs.getSlotGainReduction (2, 1)
                      << " here " << m.strips.getSlotGainReduction (2, 0) << " / " << m.strips.getSlotGainReduction (2, 1) << std::endl;
            std::cerr << "REMOTE changes reached the engine: bypass " << e.isRackBypassed (1) << " safe " << e.getRackConfig (2).recallSafe
                      << " group " << e.getRackConfig (3).latencyGroup << " colour " << e.getRackConfig (4).colourIndex
                      << " name '" << e.getRackConfig (5).name << "' slot bypass " << e.isSlotBypassed (0, 0) << std::endl;
            std::cerr << "REMOTE last rack after the copy: " << e.getNumSlots (6) << " plugins, first "
                      << (e.getPlugin (6, 0) != nullptr ? e.getPlugin (6, 0)->getName() : juce::String ("-")) << "; the copy sees "
                      << live.getNumSlots (6) << ", first " << live.getSlot (6, 0).name << std::endl;
            std::cerr << "REMOTE and back in the copy: bypass " << live.isRackBypassed (1) << " safe " << live.getRackConfig (2).recallSafe
                      << " group " << live.getRackConfig (3).latencyGroup << " colour " << live.getRackConfig (4).colourIndex
                      << " name '" << live.getRackConfig (5).name << "' slot bypass " << live.getSlot (0, 0).bypassed << std::endl;
        });
    }

    // Overview / Rack and scrolling a long plugin list, with real mouse events
    static juce::MouseEvent mouseAt (juce::Component& c, juce::Point<int> p, int clicks)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p.toFloat(), {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &c, &c, now, p.toFloat(), now, clicks, false);
    }

    static void overviewTest (MainComponent& m)
    {
        if (auto* top = m.getTopLevelComponent())
            top->setSize (1280, 720);

        // A long chain on the first rack
        juce::String error;
        for (int i = 0; i < 9; ++i)
            m.engine.copyPlugin (0, 0, 0, m.engine.getNumSlots (0), error);

        juce::Timer::callAfterDelay (3500, [&m]
        {
            std::cerr << "OVERVIEW full view " << m.isFullView() << " strips shown " << m.strips.isVisible() << " panel shown " << m.dock.isVisible()
                      << " strips height " << m.strips.getHeight() << " rack 0 plugins " << m.engine.getNumSlots (0)
                      << " overview lit " << m.overviewButton.getToggleState() << " rack lit " << m.rackViewButton.getToggleState() << std::endl;
            auto* content = m.strips.viewport.getViewedComponent();
            const auto first = content->getLocalArea (nullptr, m.strips.getSlotScreenBounds (0, 0));
            std::cerr << "OVERVIEW slot 0 at y " << first.getY() << std::endl;
            content->mouseWheelMove (mouseAt (*content, first.getCentre(), 0), juce::MouseWheelDetails { 0.0f, -1.5f, false, false, false });
            const auto after = content->getLocalArea (nullptr, m.strips.getSlotScreenBounds (0, 0));
            std::cerr << "OVERVIEW after scrolling down, slot 0 at y " << after.getY() << std::endl;
        });

        juce::Timer::callAfterDelay (6500, [&m]
        {
            auto* content = m.strips.viewport.getViewedComponent();
            const auto slot = content->getLocalArea (nullptr, m.strips.getSlotScreenBounds (1, 0));
            content->mouseDown (mouseAt (*content, slot.getCentre(), 1));
            content->mouseUp (mouseAt (*content, slot.getCentre(), 1));
            std::cerr << "OVERVIEW single click: full view " << m.isFullView() << " selected " << m.selectedRack << ":" << m.selectedSlot << std::endl;
            content->mouseDoubleClick (mouseAt (*content, slot.getCentre(), 2));
            std::cerr << "OVERVIEW double click: full view " << m.isFullView() << " panel shown " << m.dock.isVisible()
                      << " editor " << (m.dock.editor != nullptr) << " rack lit " << m.rackViewButton.getToggleState() << std::endl;
        });

        juce::Timer::callAfterDelay (9500, [&m]
        {
            m.overviewButton.triggerClick();
            juce::Timer::callAfterDelay (300, [&m]
            {
                std::cerr << "OVERVIEW back: full view " << m.isFullView() << " editor kept " << (m.dock.editor != nullptr)
                          << " overview lit " << m.overviewButton.getToggleState() << std::endl;
                // A Hot Plugin / User Key opening a plugin goes to Rack
                m.openSlotId (m.engine.getSlotId (2, 1));
                std::cerr << "OVERVIEW open by id: full view " << m.isFullView() << " selected " << m.selectedRack << ":" << m.selectedSlot << std::endl;
                m.setFullView (false);
            });
        });
    }

    // The Snapshots screen in a second window, over the network
    struct RemoteSnapshotsWindow : public juce::DocumentWindow, private juce::ChangeListener
    {
        explicit RemoteSnapshotsWindow (int port)
            : juce::DocumentWindow ("Snapshots over the network (test)", Theme::background, 0), live ("127.0.0.1", port)
        {
            setUsingNativeTitleBar (true);
            SnapshotsPanel::Host h { live,
                [this] { return (int) live.getState()["scene"]; },
                [this] (int n) { auto* o = new juce::DynamicObject(); o->setProperty ("cmd", "scene"); o->setProperty ("number", n); live.sendCommand (juce::var (o)); },
                [this] (int n, const juce::String& name, const SnapshotStoreFilter&)
                {
                    auto* o = new juce::DynamicObject(); o->setProperty ("cmd", "sceneStore"); o->setProperty ("number", n); o->setProperty ("name", name);
                    live.sendCommand (juce::var (o));
                },
                [] (const juce::String&, const juce::String&, const juce::String&, std::function<void()> ok) { ok(); },
                [] (int) { return juce::String(); },
                [] (const juce::String&) {},
                [] { return juce::String(); } };
            panel = new SnapshotsPanel (std::move (h));
            setContentOwned (panel, false);
            setBounds (0, 0, 1440, 960);
            setVisible (true);
            live.addChangeListener (this);
        }
        ~RemoteSnapshotsWindow() override { live.removeChangeListener (this); }
        void changeListenerCallback (juce::ChangeBroadcaster*) override { panel->refresh(); }

        RemoteConnection live;
        SnapshotsPanel* panel = nullptr;
    };

    static inline std::unique_ptr<RemoteSnapshotsWindow> remoteSnapshots;

    static void openRemoteSnapshots (MainComponent& m)
    {
        remoteSnapshots = std::make_unique<RemoteSnapshotsWindow> (m.webRemote.getPort());
        auto& live = remoteSnapshots->live;
        auto& e = m.engine;

        juce::Timer::callAfterDelay (2000, [&live] { remoteSnapshots->panel->select (2); live.watchSnapshot (2); });

        juce::Timer::callAfterDelay (3500, [&m, &live, &e]
        {
            juce::String a, b;
            for (auto& s : live.getSnapshots()) a << s.number << ":" << s.name << ":" << s.consoleScene << ":" << s.midi << " ";
            for (auto& s : e.getSnapshots())    b << s.number << ":" << s.name << ":" << e.getSnapshotConsoleScene (s.number) << ":" << m.midi->describeTriggerFor (s.number) << " ";
            std::cerr << "RSNAP list same " << (a == b) << " : " << a << std::endl;

            const auto dl = live.differencesFromNow (2), de = e.differencesFromNow (2);
            bool same = dl.size() == de.size();
            for (size_t i = 0; same && i < dl.size(); ++i)
                same = dl[i].rack == de[i].rack && dl[i].slot == de[i].slot && dl[i].changedSettings == de[i].changedSettings
                       && dl[i].bypassDiffers == de[i].bypassDiffers && dl[i].details == de[i].details;
            std::cerr << "RSNAP differences for 2: " << dl.size() << " same as the engine " << same << std::endl;

            remoteSnapshots->panel->select (3);   // the screen asks the server for 3's details
        });

        juce::Timer::callAfterDelay (5000, [&m, &live, &e]
        {
            bool same = true;
            for (int r = 0; r < e.getNumRacks(); ++r)
                for (int s = 0; s < e.getNumSlots (r); ++s)
                    same = same && live.isSlotInScopeFor (3, r, s) == e.isSlotInScopeFor (3, r, s) && live.snapshotHasSlot (3, r, s) == e.snapshotHasSlot (3, r, s);
            const auto sl = live.getScopeFor (3), se = e.getScopeFor (3);
            same = same && sl.pluginSettings == se.pluginSettings && sl.pluginBypass == se.pluginBypass && sl.rackBypass == se.rackBypass && sl.bpm == se.bpm;
            const auto ll = live.getSnapshotLink(), le = m.midi->getSnapshotLink();
            std::cerr << "RSNAP scope of 3 same " << same << " (kick in " << live.isSlotInScopeFor (3, 1, 0) << ")"
                      << "; MIDI link same " << (ll.follow == le.follow && ll.channel == le.channel && ll.firstSnapshot == le.firstSnapshot)
                      << " inputs " << live.getMidiInputNames().joinIntoString (",") << std::endl;

            // Changes from the network
            live.renameSnapshot (1, "Walk-in (remote)");
            auto sc = live.getScopeFor (3); sc.bpm = false; live.setScopeFor (3, sc);
            live.setSlotInScopeFor (3, 1, 0, true);
            std::cerr << "RSNAP duplicate 3 -> 4 sent " << live.duplicateSnapshot (3, 4) << ", renumber 10 -> 1 refused " << ! live.renumberSnapshot (10, 1) << std::endl;
            live.renumberSnapshot (10, 12);
            live.setSnapshotConsoleScene (2, "5.10");
            auto link = live.getSnapshotLink(); link.channel = 2; link.firstSnapshot = 5; live.setSnapshotLink (link);
            auto dflt = live.getScopeFor (-1);
            dflt.rackBypass = false; live.setScopeFor (-1, dflt);
        });

        juce::Timer::callAfterDelay (6500, [&m, &e, &live]
        {
            juce::String list;
            for (auto& s : e.getSnapshots()) list << s.number << ":" << s.name << " ";
            std::cerr << "RSNAP engine now: " << list << std::endl;
            std::cerr << "RSNAP 3 bpm " << e.getScopeFor (3).bpm << " (expect 0), kick in 3 " << e.isSlotInScopeFor (3, 1, 0) << " (expect 1), 2 linked to '"
                      << e.getSnapshotConsoleScene (2) << "', MIDI ch " << m.midi->getSnapshotLink().channel << " first " << m.midi->getSnapshotLink().firstSnapshot
                      << ", new snapshots recall rack bypass " << e.getScopeFor (-1).rackBypass << " (expect 0)" << std::endl;
            juce::String seen;
            for (auto& s : live.getSnapshots()) seen << s.number << ":" << s.name << " ";
            std::cerr << "RSNAP the copy sees: " << seen << std::endl;
        });
    }

    // The routing matrix in a second window, over the network
    struct RemoteRoutingWindow : public juce::DocumentWindow, private juce::ChangeListener
    {
        explicit RemoteRoutingWindow (int port)
            : juce::DocumentWindow ("Routing over the network (test)", Theme::background, 0), live ("127.0.0.1", port)
        {
            setUsingNativeTitleBar (true);
            matrix = new RoutingMatrix (live);
            setContentOwned (matrix, false);
            setBounds (0, 0, 1440, 960);
            setVisible (true);
            live.addChangeListener (this);
        }
        ~RemoteRoutingWindow() override { live.removeChangeListener (this); }
        void changeListenerCallback (juce::ChangeBroadcaster*) override { matrix->refresh(); }

        RemoteConnection live;
        RoutingMatrix* matrix = nullptr;
    };

    static inline std::unique_ptr<RemoteRoutingWindow> remoteRouting;

    static void openRemoteRouting (MainComponent& m)
    {
        // A console card on channels 9-24, for the block labels
        auto cs = m.consoles[0]->getSettings();
        cs.type = ConsoleLink::Type::yamahaRcp;
        cs.slots = { { "MY Slot 1", 2, 0, 9, 16 } };
        m.consoles[0]->setSettings (cs);

        remoteRouting = std::make_unique<RemoteRoutingWindow> (m.webRemote.getPort());
        auto& live = remoteRouting->live;
        auto& e = m.engine;

        juce::Timer::callAfterDelay (3000, [&m, &live, &e]
        {
            bool same = live.getNumDrivers() == e.getNumDrivers();
            for (int d = 0; d < e.getNumDrivers(); ++d)
            {
                const auto a = live.getDriver (d), b = m.live.getDriver (d);
                same = same && a.label == b.label && a.open == b.open && a.numIns == b.numIns && a.numOuts == b.numOuts
                       && a.inNames == b.inNames && a.outNames == b.outNames && a.blockLabels == b.blockLabels;
                std::cerr << "RROUTE driver " << d << " '" << a.label << "' open " << a.open << " ins " << a.numIns << " outs " << a.numOuts
                          << " blocks " << a.blockLabels.joinIntoString ("|") << std::endl;
            }
            std::cerr << "RROUTE drivers same as here " << same << std::endl;
            remoteRouting->matrix->setSelectedRack (1);

            const int in[2] = { 20, -1 }, out[2] = { 21, 22 };
            live.setRackRouting (1, 0, in, out);
            const int none[2] = { -1, -1 };
            live.setRackRouting (2, 0, none, none);
        });

        juce::Timer::callAfterDelay (4500, [&live, &e]
        {
            const auto a = e.getRackConfig (1), b = e.getRackConfig (2);
            std::cerr << "RROUTE rack 1 now in " << a.input[0] << " out " << a.output[0] << "," << a.output[1] << " (expect 20, 21,22); rack 2 in "
                      << b.input[0] << " out " << b.output[0] << " (expect -1 -1)" << std::endl;
            const auto c = live.getRackConfig (1), d = live.getRackConfig (2);
            std::cerr << "RROUTE the copy sees rack 1 in " << c.input[0] << " out " << c.output[0] << "," << c.output[1] << "; rack 2 in " << d.input[0] << std::endl;
        });
    }

    // Devices (the driver cards) and a driver's settings, over the network
    struct RemoteDevicesWindow : public juce::DocumentWindow, private juce::ChangeListener
    {
        RemoteDevicesWindow (MainComponent& m, int port)
            : juce::DocumentWindow ("Devices over the network (test)", Theme::background, 0), live ("127.0.0.1", port)
        {
            setUsingNativeTitleBar (true);
            DevicesPanel::Host h { live, &m.engine, m.midi.get(),
                [] (int) {}, [this] (int c) { openConsole (c); }, [] {}, [] { return 0; } };
            panel = new DevicesPanel (std::move (h));
            setContentOwned (panel, false);
            setBounds (0, 0, 1440, 960);
            setVisible (true);
            live.addChangeListener (this);
        }
        ~RemoteDevicesWindow() override { live.removeChangeListener (this); }
        void changeListenerCallback (juce::ChangeBroadcaster*) override { panel->refresh(); }

        void openConsole (int c)
        {
            consoleWindow = std::make_unique<juce::DocumentWindow> ("Console settings over the network", Theme::panel, juce::DocumentWindow::closeButton);
            consolePanel = new ConsoleLinkPanel (live, c);
            consoleWindow->setContentOwned (consolePanel, true);
            consoleWindow->setTopLeftPosition (700, 80);
            consoleWindow->setVisible (true);
        }

        RemoteConnection live;
        DevicesPanel* panel = nullptr;
        std::unique_ptr<juce::DocumentWindow> consoleWindow;
        ConsoleLinkPanel* consolePanel = nullptr;
        std::unique_ptr<juce::DocumentWindow> settingsWindow;
        DriverSettingsPanel* settings = nullptr;
    };

    static inline std::unique_ptr<RemoteDevicesWindow> remoteDevices;

    static void openRemoteDevices (MainComponent& m)
    {
        remoteDevices = std::make_unique<RemoteDevicesWindow> (m, m.webRemote.getPort());
        auto& live = remoteDevices->live;
        auto& e = m.engine;

        juce::Timer::callAfterDelay (2500, [&m, &live, &e]
        {
            const auto a = live.getDriverStatus (0), b = e.getDriverStatus (0);
            std::cerr << "RDEV status same " << (a.open == b.open && a.deviceName == b.deviceName && a.typeName == b.typeName && a.bufferSize == b.bufferSize
                                                 && a.numIns == b.numIns && a.numOuts == b.numOuts && a.dropouts == b.dropouts && a.threads == b.threads)
                      << " open " << a.open << " type '" << a.typeName << "' threads " << a.threads << std::endl;
            const auto h1 = live.getHeaviestPlugins (0, 3), h2 = e.getHeaviestPlugins (0, 3);
            std::cerr << "RDEV heaviest " << h1.size() << " / " << h2.size() << (h1.empty() ? juce::String() : " first " + live.getSlot (h1[0].rack, h1[0].slot).name) << std::endl;

            auto* w = new juce::DocumentWindow ("Audio driver 1 on " + live.getServerName(), Theme::panel, juce::DocumentWindow::closeButton);
            remoteDevices->settings = new DriverSettingsPanel (live, 0);
            w->setContentOwned (remoteDevices->settings, true);
            w->setTopLeftPosition (460, 560);
            w->setVisible (true);
            remoteDevices->settingsWindow.reset (w);
        });

        juce::Timer::callAfterDelay (4000, [&m, &live, &e]
        {
            const auto a = live.getDriverOptions (0), b = m.live.getDriverOptions (0);
            bool same = a.known && a.type == b.type && a.device == b.device && a.types.size() == b.types.size() && a.rates == b.rates && a.buffers == b.buffers;
            juce::String types;
            for (size_t i = 0; same && i < a.types.size(); ++i)
                same = a.types[i].name == b.types[i].name && a.types[i].devices == b.types[i].devices;
            for (auto& t : a.types) types << t.name << "(" << t.devices.size() << ") ";
            std::cerr << "RDEV options known " << a.known << " same " << same << " type '" << a.type << "' types " << types
                      << "; dialog shows " << remoteDevices->settings->type.getNumItems() << " types, picked '" << remoteDevices->settings->type.getText()
                      << "', status '" << remoteDevices->settings->status << "'" << std::endl;
            std::cerr << "RDEV add driver sent " << live.addDriver() << std::endl;
            if (a.types.size() > 1)
                live.setDriver (0, a.types[a.type == a.types[0].name ? 1 : 0].name, {}, 0, 0);
        });

        juce::Timer::callAfterDelay (5500, [&m, &live, &e]
        {
            std::cerr << "RDEV drivers now " << e.getNumDrivers() << " (expect 2), the copy sees " << live.getNumDrivers()
                      << ", cards " << live.getNumDrivers() << "; driver 1 type now '" << e.getDeviceManager (0).getCurrentAudioDeviceType() << "'" << std::endl;
            live.removeDriver (1);
        });

        juce::Timer::callAfterDelay (7000, [&live, &e]
        {
            std::cerr << "RDEV after remove: " << e.getNumDrivers() << " (expect 1), the copy sees " << live.getNumDrivers() << std::endl;
        });
    }

    // The server driver dialog against made-up devices (this computer has none)
    struct FakeServer : public LocalConnection
    {
        using LocalConnection::LocalConnection;
        bool isLocal() const override { return false; }
        juce::String getServerName() const override { return "dfproduction"; }
        LiveDriverOptions getDriverOptions (int) override
        {
            LiveDriverOptions o;
            o.known = true;
            o.type = type; o.device = device;
            o.types = { { "ASIO", { "Waves SoundGrid", "Dante Virtual Soundcard" } }, { "Windows Audio", { "Speakers" } } };
            if (device.isNotEmpty()) { o.rates = { 44100.0, 48000.0, 96000.0 }; o.buffers = { 32, 64, 128, 256 }; o.rate = rate; o.buffer = buffer; }
            return o;
        }
        juce::String setDriver (int, const juce::String& t, const juce::String& d, double r, int b) override
        {
            std::cerr << "DLG set type '" << t << "' device '" << d << "' rate " << r << " buffer " << b << std::endl;
            if (t.isNotEmpty()) { type = t; device = {}; }
            if (d.isNotEmpty()) device = d;
            if (r > 0) rate = r;
            if (b > 0) buffer = b;
            return {};
        }
        DriverStatus getDriverStatus (int) override { DriverStatus s; s.open = device.isNotEmpty(); s.numIns = s.numOuts = 64; return s; }
        juce::String type { "ASIO" }, device { "Waves SoundGrid" };
        double rate = 48000.0; int buffer = 64;
    };

    static inline std::unique_ptr<FakeServer> fakeServer;
    static inline std::unique_ptr<juce::DocumentWindow> dialogWindow;

    static void driverDialogTest (MainComponent& m)
    {
        fakeServer = std::make_unique<FakeServer> (m.engine);
        auto* panel = new DriverSettingsPanel (*fakeServer, 0);
        dialogWindow = std::make_unique<juce::DocumentWindow> ("Audio driver 1 on dfproduction", Theme::panel, juce::DocumentWindow::closeButton);
        dialogWindow->setContentOwned (panel, true);
        dialogWindow->setTopLeftPosition (300, 200);
        dialogWindow->setVisible (true);
        dialogWindow->toFront (true);

        auto show = [panel] (const char* when)
        {
            std::cerr << "DLG " << when << ": type '" << panel->type.getText() << "' device '" << panel->device.getText() << "' (" << panel->device.getNumItems()
                      << " choices) rate '" << panel->rate.getText() << "' buffer '" << panel->buffer.getText() << "' status '" << panel->status << "'" << std::endl;
        };
        juce::Timer::callAfterDelay (1500, [panel, show] { show ("opened"); panel->buffer.setSelectedId (3); });           // 128
        juce::Timer::callAfterDelay (3500, [panel, show] { show ("after buffer"); panel->rate.setSelectedId (3); });        // 96k
        juce::Timer::callAfterDelay (5500, [panel, show] { show ("after rate"); panel->device.setSelectedId (2); });        // Dante
        juce::Timer::callAfterDelay (7500, [panel, show] { show ("after device"); panel->type.setSelectedId (2); });        // Windows Audio
        juce::Timer::callAfterDelay (9500, [show] { show ("after type"); });
    }

    // Consoles, remotes, Server mode and Server check over the network
    static void remoteSetupTest (MainComponent& m)
    {
        // A CL5 at a made-up address, plus a second console
        ConsoleLink::Settings cs;
        cs.type = ConsoleLink::Type::yamahaRcp;
        cs.address = "127.0.0.1";
        cs.name = "FOH CL5";
        m.live.setConsoleSettings (0, cs);
        m.live.addConsole();

        remoteDevices = std::make_unique<RemoteDevicesWindow> (m, m.webRemote.getPort());
        auto& live = remoteDevices->live;
        auto& here = m.live;

        juce::Timer::callAfterDelay (2500, [&live, &here]
        {
            bool same = live.getNumConsoles() == here.getNumConsoles();
            for (int i = 0; same && i < here.getNumConsoles(); ++i)
            {
                const auto a = live.getConsole (i), b = here.getConsole (i);
                same = a.displayName == b.displayName && a.statusText == b.statusText && a.connected == b.connected
                       && a.settings.address == b.settings.address && a.settings.type == b.settings.type && a.settings.slots.size() == b.settings.slots.size()
                       && a.settings.recallScenes == b.settings.recallScenes && a.settings.followSelection == b.settings.followSelection;
            }
            std::cerr << "RSET consoles " << live.getNumConsoles() << " same " << same << " first '" << live.getConsole (0).displayName
                      << "' status '" << live.getConsole (0).statusText << "'" << std::endl;

            const auto o1 = live.getOscSettings(), o2 = here.getOscSettings();
            const auto w1 = live.getWebSettings(), w2 = here.getWebSettings();
            std::cerr << "RSET osc same " << (o1.enabled == o2.enabled && o1.port == o2.port && o1.statusText == o2.statusText)
                      << " web same " << (w1.enabled == w2.enabled && w1.port == w2.port && w1.url == w2.url && w1.statusText == w2.statusText)
                      << " web active " << (w1.lastActivityMs != 0) << " '" << w1.statusText << "'" << std::endl;

            const auto s1 = live.getServerSettings(), s2 = here.getServerSettings();
            std::cerr << "RSET server same " << (s1.startAtLogin == s2.startAtLogin && s1.restartAfterCrash == s2.restartAfterCrash && s1.multiCore == s2.multiCore
                                                 && s1.cores == s2.cores && s1.loginProblem == s2.loginProblem && s1.startApps == s2.startApps) << std::endl;

            const auto c1 = live.getServerCheck(), c2 = here.getServerCheck();
            bool checkSame = c1.size() == c2.size();
            for (size_t i = 0; checkSame && i < c1.size(); ++i)
                checkSame = c1[i].title == c2[i].title && c1[i].state == c2[i].state && c1[i].fixLabel == c2[i].fixLabel;
            std::cerr << "RSET server check " << c1.size() << " items same " << checkSame << std::endl;

            live.refreshDriverOptions();   // brings the networks too

            // Changes from the network
            auto st = live.getConsole (0).settings;
            st.followSelection = false;
            st.slots[0].slot = "Renamed slot";
            live.setConsoleSettings (0, st);
            live.setOscSettings (true, 9123, {}, {});
            live.setServerSettings (s1.startAtLogin, s1.restartAfterCrash, ! s1.multiCore, juce::StringArray { "/usr/bin/true" });
            live.removeConsole (1);
            remoteDevices->openConsole (0);
            ServerCheck::showWindow (live);
        });

        juce::Timer::callAfterDelay (4500, [&m, &live, &here]
        {
            const auto c = here.getConsole (0);
            std::cerr << "RSET here now: consoles " << here.getNumConsoles() << " (expect 1), follow " << c.settings.followSelection
                      << " (expect 0), slot '" << c.settings.slots[0].slot << "', osc port " << here.getOscSettings().port
                      << " (expect 9123), multi-core " << here.getServerSettings().multiCore << ", start apps "
                      << here.getServerSettings().startApps.joinIntoString (",") << std::endl;
            std::cerr << "RSET the copy sees: consoles " << live.getNumConsoles() << ", osc '" << live.getOscSettings().statusText
                      << "', networks " << live.getNetworks().size() << " (here " << here.getNetworks().size() << ")" << std::endl;
            auto* p = remoteDevices->consolePanel;
            std::cerr << "RSET console dialog: address '" << p->addressEditor.getText() << "' status '" << p->statusLabel.getText()
                      << "' log lines " << juce::StringArray::fromLines (p->logView.getText()).size() << " (here " << here.getConsoleLog (0).size() << ")" << std::endl;
            std::function<void (juce::Component&)> find = [&find] (juce::Component& c)
            {
                if (auto* b = dynamic_cast<juce::TextButton*> (&c); b != nullptr && b->getButtonText().startsWith ("Follow") | b->getButtonText().startsWith ("Recall"))
                    std::cerr << "RSET pill '" << b->getButtonText() << "' on " << b->getToggleState() << " showing " << b->isShowing() << std::endl;
                for (auto* ch : c.getChildren()) find (*ch);
            };
            find (*remoteDevices->panel);
            juce::ignoreUnused (m);
        });
    }

    // The Plugins screen, pages and Hot Plugins over the network
    struct RemotePluginsWindow : public juce::DocumentWindow
    {
        explicit RemotePluginsWindow (int port)
            : juce::DocumentWindow ("Plugins over the network (test)", Theme::background, 0), live ("127.0.0.1", port)
        {
            setUsingNativeTitleBar (true);
            panel = new PluginsPanel (live);
            setContentOwned (panel, false);
            setBounds (0, 0, 1440, 960);
            setVisible (true);
        }
        RemoteConnection live;
        PluginsPanel* panel = nullptr;
    };

    static inline std::unique_ptr<RemotePluginsWindow> remotePlugins;

    static void remotePluginsTest (MainComponent& m)
    {
        remotePlugins = std::make_unique<RemotePluginsWindow> (m.webRemote.getPort());
        auto& live = remotePlugins->live;
        auto& here = m.live;

        juce::Timer::callAfterDelay (3000, [&live, &here, &m]
        {
            const auto a = live.getKnownPlugins(), b = here.getKnownPlugins();
            bool same = a.size() == b.size();
            for (size_t i = 0; same && i < a.size(); ++i)
                same = a[i].name == b[i].name && a[i].format == b[i].format && a[i].identifier == b[i].identifier && a[i].category == b[i].category;
            std::cerr << "RPLUG known " << a.size() << " same as here " << same << " (panel shows " << remotePlugins->panel->shown.size() << ")";
            for (auto& p : a) std::cerr << " | " << p.name << " [" << p.category << "]";
            std::cerr << std::endl;
            std::cerr << "RPLUG hot same " << (live.getHotPlugins() == here.getHotPlugins()) << " pages " << live.getPages().size() << " / " << here.getPages().size()
                      << " current " << live.getCurrentPage() << " / " << here.getCurrentPage() << std::endl;

            for (auto& p : a)
                if (p.name.startsWith ("Gain"))
                    live.forgetPlugin (p.identifier);

            std::vector<RackPage> pages = live.getPages();
            pages.push_back ({ "Band", { live.getRackConfig (0).id, live.getRackConfig (1).id } });
            live.setPages (pages);
            live.setCurrentPage ((int) pages.size() - 1);
            live.setHotPlugins ({ live.getSlot (2, 1).id, live.getSlot (0, 0).id });
            juce::ignoreUnused (m);
        });

        juce::Timer::callAfterDelay (4500, [&live, &here, &m]
        {
            bool gain = false;
            for (auto& p : here.getKnownPlugins()) gain = gain || p.name.startsWith ("Gain");
            const auto pages = m.engine.getPages();
            int r = -1, s = -1;
            m.engine.findSlot (m.engine.getHotPlugins().empty() ? 0 : m.engine.getHotPlugins()[0], r, s);
            std::cerr << "RPLUG here now: Gain still listed " << gain << " (expect 0), pages " << pages.size() << " last '" << (pages.empty() ? juce::String() : pages.back().name)
                      << "' with " << (pages.empty() ? 0 : (int) pages.back().rackIds.size()) << " racks, current page " << m.engine.getCurrentPage()
                      << ", first hot " << r << ":" << s << " (expect 2:1); the copy sees " << live.getPages().size() << " pages, current " << live.getCurrentPage()
                      << ", page tab on 'Band' rack 0 " << live.isRackOnPage (0, (int) pages.size() - 1) << std::endl;
            live.startScan (false);
        });

        for (int t = 5000; t <= 30000; t += 1000)
            juce::Timer::callAfterDelay (t, [&live, &here, t]
            {
                static bool reported = false, sawRunning = false;
                const auto st = live.getScanStatus();
                if (st.running && ! sawRunning) { sawRunning = true; std::cerr << "RPLUG scanning on the server: " << st.progress << " '" << st.current << "'" << std::endl; }
                if (! st.running && ! reported && st.message.isNotEmpty())
                {
                    reported = true;
                    bool gain = false;
                    for (auto& p : here.getKnownPlugins()) gain = gain || p.name.startsWith ("Gain");
                    juce::Timer::callAfterDelay (1500, [&live, gain, st, t]
                    {
                        bool remoteGain = false;
                        for (auto& p : live.getKnownPlugins()) remoteGain = remoteGain || p.name.startsWith ("Gain");
                        std::cerr << "RPLUG scan done by " << t << " ms: '" << st.message << "' Gain back here " << gain << ", in the panel " << remoteGain
                                  << " (" << remotePlugins->panel->all.size() << " listed)" << std::endl;
                    });
                }
            });
    }

    static void pump (RackEngine& e, juce::AudioBuffer<float>& in, juce::AudioBuffer<float>& out)
    {
        e.previewProcess (in, out);
    }

    static void run (MainComponent& m)
    {
        const auto pluginDir = juce::SystemStats::getEnvironmentVariable ("LIVERACK_PREVIEW_PLUGINS", {});
        if (pluginDir.isNotEmpty())
            buildSampleSession (m, juce::File (pluginDir));

        const auto open = juce::SystemStats::getEnvironmentVariable ("LIVERACK_PREVIEW_OPEN", {});

        if (open == "gain")     m.select (1, 0);
        if (open == "gate")     m.select (2, 0);
        if (open == "rack")     m.showRackDialog (0);
        if (open == "console")  m.showConsoleSettings (0);
        if (open == "devices")  m.showDevices();
        if (open == "routing")  { m.select (2, 0); m.showRouting (2); }
        if (open == "routingout") { m.showRouting (6); m.routingMatrix->outputs = true; m.routingMatrix->refresh(); }
        if (open == "scope")    m.showScope();
        if (open == "fullview") { m.select (0, 1); m.setFullView (true); m.stepRack (1); m.stepRack (1); m.stepRack (-1);
                                  std::cerr << "STEP selected " << m.selectedRack << " (expect 1)" << std::endl;
                                  m.select (0, 1); m.stepRack (-1);
                                  std::cerr << "STEP wrap " << m.selectedRack << " (expect " << m.engine.getNumRacks() - 1 << ")" << std::endl; m.stepRack (1); }
        if (open == "plugins")  m.showPluginScanner();
        if (open == "remote")   openRemoteWindow (m);
        if (open == "switchmode")
        {
            // Native -> Client -> Native in one window (the logo menu)
            static int times = 0;
            std::cerr << "NATIVE up (" << times << ") racks " << m.engine.getNumRacks() << std::endl;
            if (times++ == 0)
                juce::Timer::callAfterDelay (2000, [] { if (AppMode::switchTo != nullptr) AppMode::switchTo (true); });
        }
        if (open == "overview") overviewTest (m);
        if (open == "remoterouting") openRemoteRouting (m);
        if (open == "remotedevices") openRemoteDevices (m);
        if (open == "remotesetup") remoteSetupTest (m);
        if (open == "remoteplugins") remotePluginsTest (m);
        if (open == "driverdialog") driverDialogTest (m);
        if (open == "timing")
        {
            // Keep the audio going so plugin timing shows
            m.select (3, 1);
            struct Feeder : juce::Timer
            {
                MainComponent& m;
                explicit Feeder (MainComponent& mm) : m (mm) { m.engine.previewPrepare (48000.0, 128); startTimer (2); }
                void timerCallback() override
                {
                    juce::AudioBuffer<float> in (16, 128), out (16, 128);
                    for (int c = 0; c < 16; ++c) for (int k = 0; k < 128; ++k) in.setSample (c, k, r.nextFloat() * 0.4f - 0.2f);
                    m.engine.previewProcess (in, out);
                }
                juce::Random r;
            };
            static std::unique_ptr<Feeder> feeder;
            feeder = std::make_unique<Feeder> (m);
            juce::Timer::callAfterDelay (3000, [&m] { m.showDevices(); });
        }
        if (open == "servercheck")
        {
            for (auto& i : ServerCheck::getAppItems (m.engine, m.webRemote))
                std::cerr << "CHECK app " << (int) i.state << " " << i.title << " | " << i.detail << " | fix " << i.fixLabel << std::endl;
            std::cerr << "CHECK Xvfb running " << ServerMode::isAppRunning (juce::File ("/usr/bin/Xvfb"))
                      << ", nothing running " << ServerMode::isAppRunning (juce::File ("/usr/bin/not-a-thing")) << std::endl;
            ServerMode::setStartApps ({ "/usr/bin/Xvfb", "/tmp/x y/App" });
            std::cerr << "CHECK start apps " << ServerMode::getStartApps().joinIntoString (" ; ") << std::endl;
            ServerMode::setStartApps ({});
            m.showDevices();
            juce::Timer::callAfterDelay (1500, [&m] { ServerCheck::showWindow (m.live); });
        }
        if (open == "networks")
        {
            for (auto& n : SocketUtil::getNetworks())
                std::cerr << "NET " << n.id << " '" << n.name << "' " << n.address << " bcast " << n.broadcast << " linklocal " << n.linkLocal << std::endl;
            SocketUtil::Network fake;
            fake.ip = (10u << 24) | 5u; fake.mask = 0xffffff00u;
            std::cerr << "NET filter 10.0.0.77 " << SocketUtil::isOnNetworkOrLocal ("10.0.0.77", fake)
                      << " 169.254.1.1 " << SocketUtil::isOnNetworkOrLocal ("169.254.1.1", fake)
                      << " 127.0.0.1 " << SocketUtil::isOnNetworkOrLocal ("127.0.0.1", fake)
                      << " 10.0.1.2 " << SocketUtil::isOnNetworkOrLocal ("10.0.1.2", fake) << " (expect 1 0 1 0)" << std::endl;
            auto nets = SocketUtil::getNetworks();
            if (! nets.empty())
            {
                m.webRemote.setNetwork (nets[0].id, nets[0].name);
                std::cerr << "NET web on " << nets[0].name << ": " << m.webRemote.getStatusText() << std::endl;
                m.osc.setNetwork (nets[0].id, nets[0].name);
                std::cerr << "NET osc: " << m.osc.getStatusText() << std::endl;
            }
            m.webRemote.setNetwork ("missing0", "Ethernet 9");
            std::cerr << "NET missing: " << m.webRemote.getStatusText() << " listening " << m.webRemote.isListening() << std::endl;
            m.webRemote.setNetwork ({}, {});
        }
        if (open == "remotesnapshots")
            juce::Timer::callAfterDelay (600, [&m]
            {
                auto& e = m.engine;
                m.storeScene (1, "Walk-in");
                e.getPlugin (0, 1)->getParameters()[0]->setValueNotifyingHost (0.3f);
                m.storeScene (2, "Worship Set");
                m.storeScene (3, "Message");
                m.storeScene (10, "Baptism");
                e.setSlotInScopeFor (3, 1, 0, false);
                e.setSlotInScopeFor (3, 2, 0, false);
                e.setSnapshotConsoleScene (10, "4.10");
                MidiControl::SnapshotLink link;
                link.follow = true; link.channel = 1; link.firstSnapshot = 1;
                m.midi->setSnapshotLink (link);
                m.recallScene (1);
                e.getPlugin (0, 1)->getParameters()[0]->setValueNotifyingHost (0.62f);
                e.setSlotBypassed (2, 1, true);
                e.setRackBypassed (3, true);
                openRemoteSnapshots (m);
            });

        if (open == "snapshots" || open == "snapshotspick")
            juce::Timer::callAfterDelay (600, [&m, open]
            {
                auto& e = m.engine;
                m.storeScene (1, "Walk-in");
                e.getPlugin (0, 1)->getParameters()[0]->setValueNotifyingHost (0.3f);
                m.storeScene (2, "Worship Set");
                m.storeScene (3, "Message");
                m.storeScene (10, "Baptism");
                e.setSlotInScopeFor (3, 1, 0, false);
                e.setSlotInScopeFor (3, 2, 0, false);
                m.recallScene (2);
                // Change things after storing: 2 now differs
                e.getPlugin (0, 1)->getParameters()[0]->setValueNotifyingHost (0.62f);
                e.getPlugin (0, 1)->getParameters()[1]->setValueNotifyingHost (0.4f);
                e.setSlotBypassed (2, 1, true);
                e.setRackBypassed (3, true);
                m.showScope();
                auto d = e.differencesFromNow (2);
                std::cerr << "SNAP differences for 2: " << d.size() << std::endl;
                for (auto& x : d)
                    std::cerr << "SNAP  rack " << x.rack << " slot " << x.slot << " settings " << x.changedSettings << " bypass " << x.bypassDiffers
                              << " : " << x.details.joinIntoString (" | ") << std::endl;
                // Program changes recall snapshots
                {
                    MidiControl::SnapshotLink link;
                    link.follow = true;
                    link.channel = 1;
                    link.firstSnapshot = 1;
                    m.midi->setSnapshotLink (link);
                    m.midi->handle ({ "Test device", juce::MidiMessage::programChange (1, 2) });  // program 3
                    std::cerr << "SNAP program 3 ch1 -> current " << m.currentScene << " (expect 3)" << std::endl;
                    m.midi->handle ({ "Test device", juce::MidiMessage::programChange (2, 0) });  // other channel: ignored
                    std::cerr << "SNAP program 1 ch2 ignored -> current " << m.currentScene << " (expect 3)" << std::endl;
                    link.firstSnapshot = 0; // a device that counts from 0
                    m.midi->setSnapshotLink (link);
                    m.midi->handle ({ "Test device", juce::MidiMessage::programChange (1, 10) });
                    std::cerr << "SNAP first=0, program 11 -> current " << m.currentScene << " (expect 10)" << std::endl;
                    std::cerr << "SNAP trigger text for 10: '" << m.midi->describeTriggerFor (10) << "'" << std::endl;
                    link.firstSnapshot = 1;
                    link.send = true;
                    link.output = "Nothing plugged in";
                    m.midi->setSnapshotLink (link);
                    m.recallScene (2); // no output: nothing sent, no crash
                    link.follow = false; link.send = false;
                    m.midi->setSnapshotLink (link);
                    m.recallScene (2);
                }
                if (open == "snapshotspick")
                    juce::Timer::callAfterDelay (300, [&m]
                    {
                        auto& e = m.engine;
                        // store only the ticked: untick the Snare bypass, keep the comp change
                        SnapshotStoreFilter f;
                        f.everything = false;
                        f.slotIds.push_back (e.getSlotId (0, 1));
                        m.storeScene (2, {}, f);
                        auto d = e.differencesFromNow (2);
                        std::cerr << "SNAP after storing only Lead Vox comp: " << d.size() << " differences left (expect 2: snare bypass, drum bus rack bypass)" << std::endl;
                        for (auto& x : d)
                            std::cerr << "SNAP  left rack " << x.rack << " slot " << x.slot << " : " << x.details.joinIntoString (" | ") << std::endl;
                        std::cerr << "SNAP scope kept: 3 has kick out " << ! e.isSlotInScopeFor (3, 1, 0) << std::endl;
                        m.storeScene (3, {}, {});
                        std::cerr << "SNAP after store over 3: kick still out " << ! e.isSlotInScopeFor (3, 1, 0) << std::endl;
                        std::cerr << "SNAP duplicate 3 -> " << e.nextFreeSnapshotNumber (3) << " ok " << e.duplicateSnapshot (3, e.nextFreeSnapshotNumber (3))
                                  << ", renumber 10 -> 3 refused " << ! e.renumberSnapshot (10, 3) << ", 10 -> 12 ok " << e.renumberSnapshot (10, 12) << std::endl;
                        juce::String names;
                        for (auto& sn : e.getSnapshots()) names << sn.number << ":" << sn.name << " ";
                        std::cerr << "SNAP list " << names << std::endl;
                    });
            });
        if (open == "devpop")   { m.showDevices(); m.devicesWindow->setPoppedOut (true); }
        if (open == "toolclose") { m.showDevices(); juce::Timer::callAfterDelay (2000, [&m] { m.devicesWindow->close(); }); }
        if (open == "toolstack") { m.showDevices(); m.showScope(); m.scopeWindow->close(); }
        if (open == "groupmenu")  juce::Timer::callAfterDelay (1500, [&m] { m.strips.previewMenu (0, 0); });
        if (open == "colourmenu") juce::Timer::callAfterDelay (1500, [&m] { m.strips.previewMenu (1, 1); });
        if (open == "keymenu2")   juce::Timer::callAfterDelay (1500, [&m] { Action::showChooser (m.engine, {}, 0, [] (Action) {}); });
        if (open == "keymenu")  { Action::showChooser (m.engine, {}, 0, [] (Action) {}); }
        if (open == "rename")   m.strips.previewMenu (2, 2);
        if (open == "store")    m.showStoreSceneDialog();
        if (open == "slotmenu") m.showSlotMenu (5, 1, m.strips.getSlotScreenBounds (5, 1));
        if (open == "rackmenu") m.showRackMenu (4);
        if (open == "comp")     m.select (0, 1);
        if (open == "toggle")   juce::Timer::callAfterDelay (1500, [&m] { m.select (0, 1); m.setTwoWindowMode (true); m.setTwoWindowMode (false); m.select (0, 2); });
        if (open == "popout")   juce::Timer::callAfterDelay (1500, [&m] { m.select (0, 2); m.popOutEditor (0, 2); });
        if (open == "picker")
        {
            m.showAddPluginMenu (0);
            juce::Timer::callAfterDelay (2000, []
            {
                std::cerr << "PICKER modal count=" << juce::Component::getNumCurrentlyModalComponents()
                          << " desktop comps=" << juce::Desktop::getInstance().getNumComponents() << std::endl;
                for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i)
                    if (auto* c = juce::Desktop::getInstance().getComponent (i))
                        std::cerr << "  desktop: " << typeid (*c).name() << " " << c->getBounds().toString()
                                  << " visible=" << c->isVisible() << std::endl;
            });
        }
        if (open == "params")   { m.select (0, 1); m.showParameters (0, 1); }
        if (open == "roundtrip")
        {
            auto xml = m.engine.createShow();
            m.closeAllEditors();
            m.engine.clearAll();
            juce::String error;
            const bool ok = m.engine.loadShow (*xml, error);
            std::cerr << "ROUNDTRIP full reload ok=" << ok << " racks=" << m.engine.getNumRacks()
                      << " error='" << error << "'" << std::endl;
            std::cerr << "ROUNDTRIP second load is in-place=" << m.engine.snapshotMatchesCurrentLayout (*xml) << std::endl;
            m.select (0, 1);
        }

        if (open == "showfile")
            juce::Timer::callAfterDelay (800, [&m]
            {
                auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Test Show.liverack");
                m.engine.setShowFile (f);
                m.storeScene (12, "Verse");
                m.saveShow (false);
                std::cerr << "SHOWFILE saved " << f.existsAsFile() << " edited " << m.engine.isShowEdited() << " title " << m.shownTitle << std::endl;
                m.engine.getPlugin (0, 1)->getParameters()[0]->setValueNotifyingHost (0.95f);
                m.engine.setSlotBypassed (1, 0, true);
                m.recallScene (12);
                std::cerr << "SHOWFILE recall note: " << m.consoleNote << " | kick comp bypassed " << m.engine.isSlotBypassed (1, 0) << std::endl;
                m.newShow();
                std::cerr << "SHOWFILE new: racks " << m.engine.getNumRacks() << " snapshots " << m.engine.getSnapshots().size() << std::endl;
                m.openShowFile (f);
                std::cerr << "SHOWFILE reopened: racks " << m.engine.getNumRacks() << " snapshots " << m.engine.getSnapshots().size()
                          << " note: " << m.consoleNote << " edited " << m.engine.isShowEdited() << std::endl;
                // Each snapshot keeps its own scope
                {
                    auto& e = m.engine;
                    auto* comp = e.getPlugin (0, 1);
                    e.setSlotInScope (0, 1, true);
                    m.storeScene (20, "Comp in");
                    e.setSlotInScope (0, 1, false);
                    m.storeScene (21, "Comp out");
                    comp->getParameters()[0]->setValueNotifyingHost (0.11f);
                    m.storeScene (22, "Comp low");
                    const float low = comp->getParameters()[0]->getValue();
                    comp->getParameters()[0]->setValueNotifyingHost (0.9f);
                    m.recallScene (21);
                    const bool keptBy21 = std::abs (comp->getParameters()[0]->getValue() - 0.9f) < 0.01f;
                    m.recallScene (20);
                    const bool setBy20 = std::abs (comp->getParameters()[0]->getValue() - 0.9f) > 0.01f;
                    std::cerr << "SHOWFILE per-snapshot scope: 21 leaves comp " << keptBy21 << ", 20 recalls it " << setBy20
                              << ", 21 scope says out " << ! e.isSlotInScopeFor (21, 0, 1) << ", 20 in " << e.isSlotInScopeFor (20, 0, 1) << std::endl;
                    // editing a stored snapshot's scope
                    e.setSlotInScopeFor (22, 0, 1, true);
                    auto sc = e.getScopeFor (22); sc.pluginBypass = false; e.setScopeFor (22, sc);
                    comp->getParameters()[0]->setValueNotifyingHost (0.9f);
                    e.setSlotBypassed (0, 1, true);
                    std::cerr << "SHOWFILE before recall bypassed " << e.isSlotBypassed (0, 1) << " scopeFor22.bypass " << e.getScopeFor (22).pluginBypass << std::endl;
                    m.recallScene (22);
                    std::cerr << "SHOWFILE edited scope of 22: comp back to low " << (std::abs (comp->getParameters()[0]->getValue() - low) < 0.01f)
                              << ", bypass left on " << e.isSlotBypassed (0, 1) << ", default scope bypass still " << e.getScope().pluginBypass << std::endl;
                    e.setSlotBypassed (0, 1, false);
                    // survives save / open
                    m.saveShow (false);
                    m.openShowFile (f);
                    std::cerr << "SHOWFILE after reopen: 21 out " << ! e.isSlotInScopeFor (21, 0, 1) << ", 22 bypass scope " << e.getScopeFor (22).pluginBypass << std::endl;
                    m.scopeEditor = nullptr;
                    m.currentScene = 22;
                    m.showScope();
                }
                // A plugin editor too big for the panel opens in its own window
                if (auto* top = m.getTopLevelComponent())
                    top->setSize (1300, 780);
                m.select (0, 2);
                juce::Timer::callAfterDelay (1500, [&m]
                {
                    auto* p = m.engine.getPlugin (0, 2);
                    auto* ed = p != nullptr ? p->getActiveEditor() : nullptr;
                    std::cerr << "SHOWFILE big editor " << (ed ? ed->getWidth() : 0) << "x" << (ed ? ed->getHeight() : 0)
                              << " in own window " << (ed != nullptr && ed->getTopLevelComponent() != m.getTopLevelComponent()) << std::endl;
                });
                juce::Timer::callAfterDelay (2500, [&m]
                {
                m.setTwoWindowMode (true);
                m.storeWindowLayout();
                auto* w = m.engine.getShowExtra ("WINDOWS");
                std::cerr << "SHOWFILE windows extra two=" << (w != nullptr ? w->getBoolAttribute ("twoWindows") : -1) << std::endl;
                });
            });

        if (open == "isoview")
            juce::Timer::callAfterDelay (800, [&m]
            {
                auto& e = m.engine;
                juce::String error;
                e.setSlotIsolated (0, 1, true, error);
                m.select (0, 0);
                m.refreshAll();
                auto* p = dynamic_cast<IsolatedPlugin*> (e.getPlugin (0, 1));
                std::cerr << "ISOVIEW isolated " << (p != nullptr) << " " << error << std::endl;

                struct T { std::shared_ptr<PluginViewCapture> a, b; int framesA = 0, framesB = 0; juce::String msgs; };
                auto t = std::make_shared<T>();
                juce::String err;
                t->a = m.openRemoteView ("view=0:1&scale=1", err);
                std::cerr << "ISOVIEW open a " << (t->a != nullptr) << " '" << err << "'" << std::endl;

                auto poll = [t]
                {
                    juce::MemoryBlock f; juce::String s;
                    if (t->a) { while (t->a->takeFrame (f)) { ++t->framesA; if (t->framesA == 1) std::cerr << "ISOVIEW first frame " << f.getSize() << " bytes" << std::endl; }
                                while (t->a->takeMessage (s)) t->msgs << "A:" << s << " "; }
                    if (t->b) { while (t->b->takeFrame (f)) ++t->framesB;
                                while (t->b->takeMessage (s)) t->msgs << "B:" << s << " "; }
                };
                for (int i = 1; i <= 60; ++i)
                    juce::Timer::callAfterDelay (i * 100, poll);

                juce::Timer::callAfterDelay (2000, [&m, t]
                {
                    auto* p = m.engine.getPlugin (0, 1);
                    juce::String before;
                    for (auto* q : p->getParameters()) before << q->getValue() << " ";
                    std::cerr << "ISOVIEW frames a " << t->framesA << " params before " << before << std::endl;
                    for (float y : { 0.14f, 0.43f })
                    {
                        auto ev = [y] (const char* type)
                        {
                            auto* o = new juce::DynamicObject();
                            o->setProperty ("t", type); o->setProperty ("x", 0.42); o->setProperty ("y", y);
                            o->setProperty ("b", "l"); o->setProperty ("n", 1);
                            return juce::var (o);
                        };
                        t->a->input (ev ("down"));
                        t->a->input (ev ("up"));
                    }
                    juce::String err;
                    t->b = m.openRemoteView ("view=0:1", err); // a second device
                });

                juce::Timer::callAfterDelay (3500, [&m, t]
                {
                    auto* p = m.engine.getPlugin (0, 1);
                    juce::String after;
                    for (auto* q : p->getParameters()) after << q->getValue() << " ";
                    std::cerr << "ISOVIEW params after clicks " << after << " frames a " << t->framesA << " b " << t->framesB << std::endl;
                    m.engine.crashIsolatedSlot (0, 1);
                });

                juce::Timer::callAfterDelay (5500, [&m, t]
                {
                    std::cerr << "ISOVIEW after crash: ended a " << t->a->hasEnded() << " b " << t->b->hasEnded() << " msgs " << t->msgs << std::endl;
                    m.closeRemoteView (t->a);
                    m.closeRemoteView (t->b);
                    t->a = t->b = nullptr;
                    juce::String err;
                    t->a = m.openRemoteView ("view=0:1&scale=1", err);
                    std::cerr << "ISOVIEW reopen after restart " << (t->a != nullptr) << " '" << err << "'" << std::endl;
                });

                juce::Timer::callAfterDelay (6900, [&m, t]
                {
                    std::cerr << "ISOVIEW reopened frames " << t->framesA << " msgs " << t->msgs << std::endl;
                    m.closeRemoteView (t->a);
                    t->a = nullptr;
                });
            });

        if (open == "isolate")
            juce::Timer::callAfterDelay (800, [&m]
            {
                auto& e = m.engine;
                auto iso = [&e] { return dynamic_cast<IsolatedPlugin*> (e.getPlugin (0, 1)); };
                std::cerr << "ISO before: peaks " << e.consumeSlotOutputPeak (0, 0) << " " << e.consumeSlotOutputPeak (0, 1) << " " << e.consumeSlotOutputPeak (0, 2) << std::endl;
                juce::String error;
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                const bool ok = e.setSlotIsolated (0, 1, true, error);
                std::cerr << "ISO isolate ok " << ok << " '" << error << "' in " << (int) (juce::Time::getMillisecondCounterHiRes() - t0) << " ms, isolated "
                          << e.isSlotIsolated (0, 1) << " params " << e.getPlugin (0, 1)->getParameters().size()
                          << " p0 " << e.getPlugin (0, 1)->getParameters()[0]->getValue() << std::endl;
                m.refreshAll();
                m.select (0, 1);

                juce::Timer::callAfterDelay (1500, [&m, iso]
                {
                    auto& e = m.engine;
                    auto* p = iso();
                    std::cerr << "ISO running: processed " << p->getBlocksProcessed() << " missed " << p->getBlocksMissed()
                              << " peaks " << e.consumeSlotOutputPeak (0, 0) << " " << e.consumeSlotOutputPeak (0, 1) << " " << e.consumeSlotOutputPeak (0, 2)
                              << " status " << p->getStatusText() << " name " << p->getParameters()[0]->getName (40)
                              << " text '" << p->getParameters()[0]->getCurrentValueAsText() << "' latency " << p->getLatencySamples() << std::endl;
                    // snapshot recall through the helper
                    p->getParameters()[0]->setValueNotifyingHost (0.37f);
                    juce::Timer::callAfterDelay (300, [&m, iso] { m.storeScene (30, "Iso"); });
                    juce::Timer::callAfterDelay (600, [iso] { iso()->getParameters()[0]->setValueNotifyingHost (0.8f); });
                    juce::Timer::callAfterDelay (900, [&m, iso]
                    {
                        m.recallScene (30);
                        std::cerr << "ISO right after recall p0 " << iso()->getParameters()[0]->getValue() << " (want 0.37)" << std::endl;
                        m.engine.setSlotBypassed (0, 1, true);
                    });
                    juce::Timer::callAfterDelay (1300, [&m, iso]
                    {
                        auto* p = iso();
                        std::cerr << "ISO after recall p0 " << p->getParameters()[0]->getValue() << " (want 0.37), bypassed " << m.engine.isSlotBypassed (0, 1) << std::endl;
                        m.engine.setSlotBypassed (0, 1, false);
                        p->getParameters()[0]->setValueNotifyingHost (0.61f);

                        // Audio through a helper, compared with the same plugin inside LiveRack
                        for (auto& d : m.engine.getKnownPlugins().getTypes())
                            if (d.name.containsIgnoreCase ("gain"))
                            {
                                juce::String err;
                                auto run = [] (juce::AudioPluginInstance& g)
                                {
                                    Isolation::requestLayout (g, 2, 2);
                                    g.prepareToPlay (48000.0, 256);
                                    g.getParameters()[0]->setValueNotifyingHost (0.25f);
                                    juce::AudioBuffer<float> b (2, 256);
                                    juce::MidiBuffer midi;
                                    float last = 0.0f;
                                    for (int i = 0; i < 40; ++i)
                                    {
                                        for (int c = 0; c < 2; ++c)
                                            for (int k = 0; k < 256; ++k)
                                                b.setSample (c, k, c == 0 ? 0.5f : -0.25f);
                                        g.processBlock (b, midi);
                                        last = b.getSample (0, 255) * 1000.0f + b.getSample (1, 255);
                                        juce::Thread::sleep (2);
                                    }
                                    g.releaseResources();
                                    return last;
                                };
                                auto inside = m.engine.getFormatManager().createPluginInstance (d, 48000.0, 256, err);
                                auto outside = IsolatedPlugin::create (d, 48000.0, 256, err);
                                if (inside != nullptr && outside != nullptr)
                                {
                                    const float a = run (*inside), b = run (*outside);
                                    std::cerr << "ISO audio check (" << d.name << "): inside " << a << " helper " << b << " same " << (std::abs (a - b) < 0.01f)
                                              << " missed " << outside->getBlocksMissed() << " of 40" << std::endl;
                                }
                                else std::cerr << "ISO audio check couldn't load: " << err << std::endl;
                                break;
                            }
                    });
                    juce::Timer::callAfterDelay (3200, [&m, iso]
                    {
                        const int before = iso()->getBlocksProcessed();
                        setenv ("LIVERACK_TEST_SLOW_LOAD", "3000", 1); // the next helper loads slowly
                        m.engine.crashIsolatedSlot (0, 1);
                        std::cerr << "ISO crash sent at processed " << before << std::endl;

                        // How long does LiveRack's window go without a chance to run while it restarts?
                        struct GapMeter : juce::Timer
                        {
                            juce::uint32 last = juce::Time::getMillisecondCounter(), worst = 0; int ticks = 0;
                            void timerCallback() override
                            {
                                const auto now = juce::Time::getMillisecondCounter();
                                worst = juce::jmax (worst, now - last); last = now;
                                if (++ticks == 200) { std::cerr << "ISO longest main-thread pause during restart: " << worst << " ms" << std::endl; stopTimer(); }
                            }
                        };
                        static GapMeter gap;
                        gap.startTimer (20);
                    });
                    juce::Timer::callAfterDelay (8500, [&m, iso]
                    {
                        auto& e = m.engine;
                        auto* p = iso();
                        const int a = p->getBlocksProcessed();
                        std::cerr << "ISO after crash: running " << p->isRunning() << " restarts " << p->getRestartCount() << " p0 " << p->getParameters()[0]->getValue()
                                  << " (want 0.61) missed " << p->getBlocksMissed() << " note: " << m.consoleNote << std::endl;
                        juce::Timer::callAfterDelay (500, [&m, iso, a]
                        {
                            auto& e = m.engine;
                            std::cerr << "ISO processing again: +" << iso()->getBlocksProcessed() - a << " blocks, racks " << e.getNumRacks() << std::endl;
                            auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Iso Test.liverack");
                            f.deleteFile();
                            e.setShowFile (f);
                            m.saveShow (false);
                            m.newShow();
                            m.openShowFile (f);
                            std::cerr << "ISO reopened: isolated " << e.isSlotIsolated (0, 1) << " others " << e.isSlotIsolated (0, 0)
                                      << " p0 " << e.getPlugin (0, 1)->getParameters()[0]->getValue() << std::endl;
                            juce::String err;
                            e.setSlotIsolated (0, 1, false, err);
                            std::cerr << "ISO back inside: isolated " << e.isSlotIsolated (0, 1) << " p0 " << e.getPlugin (0, 1)->getParameters()[0]->getValue() << std::endl;
                            e.setSlotIsolated (0, 1, true, err);
                            m.refreshAll();
                            m.select (0, 1);
                        });
                    });
                });
            });

        if (open == "servertest")
        {
            std::cerr << "SERVER restarted-after-crash " << ServerMode::wasRestartedAfterCrash() << " note '" << m.consoleNote
                      << "' waiting for '" << m.engine.getDriverWaitingFor (0) << "'" << std::endl;
            juce::Timer::callAfterDelay (4000, [&m]
            {
                m.engine.saveSettings();
                std::cerr << "SERVER quitting properly" << std::endl;
                juce::JUCEApplicationBase::quit();
            });
        }

        if (open == "dm7")
            juce::Timer::callAfterDelay (600, [&m]
            {
                // Backups: two saves with a change between make one backup
                auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Backup Test.liverack");
                f.getParentDirectory().getChildFile ("Backup Test Backups").deleteRecursively();
                f.deleteFile();
                m.engine.setShowFile (f);
                m.saveShow (false);
                m.saveShow (false); // same content again
                m.storeScene (5, "Five");
                m.saveShow (false);
                const auto backups = f.getParentDirectory().getChildFile ("Backup Test Backups")
                                      .findChildFiles (juce::File::findFiles, false, "*.liverack");
                std::cerr << "BACKUPS count " << backups.size() << std::endl;

                ConsoleLink::Settings s;
                s.type = ConsoleLink::Type::yamahaDm7;
                s.address = "127.0.0.1";
                m.consoles[0]->setSettings (s);
                m.engine.getPlugin (0, 1)->getParameters()[0]->setValueNotifyingHost (0.95f);
                juce::Timer::callAfterDelay (4000, [&m]
                {
                    std::cerr << "DM7 note: " << m.consoleNote << " | status " << m.consoles[0]->getStatusText() << std::endl;
                    for (auto& l : m.consoles[0]->getLog())
                        std::cerr << "DM7 log: " << l << std::endl;
                });
            });

        if (open == "cuefollow")
            juce::Timer::callAfterDelay (600, [&m]
            {
                // Racks on console channels 4 and 12; a fake CL5 cues them
                auto c2 = m.engine.getRackConfig (2); c2.consoleChannel = 4; m.engine.setRackConfig (2, c2);
                auto c5 = m.engine.getRackConfig (5); c5.consoleChannel = 12; m.engine.setRackConfig (5, c5);
                m.select (0, 0);
                ConsoleLink::Settings s;
                s.type = ConsoleLink::Type::yamahaRcp;
                s.address = "127.0.0.1";
                s.recallScenes = true;
                s.followSelection = true;
                m.consoles[0]->setSettings (s);
                for (int t : { 2500, 3500, 4500, 5500 })
                    juce::Timer::callAfterDelay (t, [&m, t]
                    {
                        std::cerr << "CUE at " << t << " ms: selected rack " << m.selectedRack << " (" << m.engine.getRackConfig (m.selectedRack).name << ")" << std::endl;
                        if (t == 4500)
                        {
                            auto st = m.consoles[0]->getSettings(); st.followSelection = false; m.consoles[0]->setSettings (st);
                        }
                    });
                juce::Timer::callAfterDelay (6000, [&m]
                {
                    for (auto& l : m.consoles[0]->getLog())
                        std::cerr << "CUE log: " << l << std::endl;
                });
            });

        if (open == "pointscenes")
            juce::Timer::callAfterDelay (600, [&m]
            {
                m.storeScene (4, "Four");
                m.storeScene (5, "Five");
                m.storeScene (20, "Four point one");
                m.engine.setSnapshotConsoleScene (20, "4.1");
                std::cerr << "POINT link of 20: " << m.engine.getSnapshotConsoleScene (20)
                          << ", find 4.10 -> " << m.engine.findSnapshotForConsoleScene ("4.10") << std::endl;
                m.engine.duplicateSnapshot (20, 21);
                std::cerr << "POINT duplicate keeps no link: '" << m.engine.getSnapshotConsoleScene (21) << "'" << std::endl;
                ConsoleLink::Settings s;
                s.type = ConsoleLink::Type::yamahaDm7;
                s.address = "127.0.0.1";
                s.recallScenes = true;
                m.consoles[0]->setSettings (s);
                for (int t : { 2500, 3500, 4500 })
                    juce::Timer::callAfterDelay (t, [&m, t]
                    {
                        std::cerr << "POINT at " << t << " ms: current snapshot " << m.currentScene
                                  << ", console said " << m.consoles[0]->getLastSceneText() << std::endl;
                    });
            });

        if (open == "multicore")
            juce::Timer::callAfterDelay (600, [&m]
            {
                auto& e = m.engine;
                e.previewPrepare (48000.0, 128);
                juce::AudioBuffer<float> in (16, 128), out (16, 128);
                juce::Random rnd (1);
                auto timeIt = [&] (bool multi)
                {
                    e.setMultiCore (multi);
                    for (int i = 0; i < 200; ++i) e.previewProcess (in, out); // warm up
                    double worst = 0, total = 0;
                    for (int i = 0; i < 2000; ++i)
                    {
                        for (int c = 0; c < 16; ++c)
                            for (int k = 0; k < 128; ++k)
                                in.setSample (c, k, rnd.nextFloat() * 0.5f - 0.25f);
                        const auto t0 = juce::Time::getMillisecondCounterHiRes();
                        e.previewProcess (in, out);
                        const auto dt = juce::Time::getMillisecondCounterHiRes() - t0;
                        worst = juce::jmax (worst, dt); total += dt;
                    }
                    std::cerr << "MULTICORE " << (multi ? "on " : "off") << " threads " << e.getDriverStatus (0).threads
                              << " avg " << total / 2000.0 * 1000.0 << " us, worst " << worst * 1000.0 << " us, out rms " << out.getRMSLevel (0, 0, 128) << std::endl;
                };
                // Each block is checked: same output with and without the rack threads
                {
                    juce::AudioBuffer<float> a (16, 128), b (16, 128), x (16, 128);
                    juce::Random r2 (7);
                    for (int c = 0; c < 16; ++c) for (int k = 0; k < 128; ++k) x.setSample (c, k, r2.nextFloat() * 0.5f - 0.25f);
                    float maxDiff = 0;
                    for (int i = 0; i < 50; ++i)
                    {
                        // Two identical engines aren't available, so compare a block against itself after
                        // the delay lines settle: the same input repeated gives the same steady output
                        e.setMultiCore (i % 2 == 0);
                        e.previewProcess (x, i % 2 == 0 ? a : b);
                        if (i > 40 && i % 2 == 1)
                            for (int c = 0; c < 16; ++c)
                                for (int k = 0; k < 128; ++k)
                                    maxDiff = juce::jmax (maxDiff, std::abs (a.getSample (c, k) - b.getSample (c, k)));
                    }
                    std::cerr << "MULTICORE output on vs off (repeated input) max diff " << maxDiff << " rms " << a.getRMSLevel (6, 0, 128) << std::endl;
                }
                std::cerr << "MULTICORE racks " << e.getNumRacks() << " cores " << juce::SystemStats::getNumPhysicalCpus() << std::endl;
                timeIt (false); timeIt (true); timeIt (false); timeIt (true);
                e.updateDriverStats();
                for (int r = 0; r < e.getNumRacks(); ++r)
                {
                    auto l = e.getRackLoad (r);
                    juce::String slots;
                    float sum = 0;
                    for (int sl = 0; sl < e.getNumSlots (r); ++sl)
                    {
                        const auto pl = e.getSlotLoad (r, sl).peak;
                        sum += pl;
                        slots << e.getPlugin (r, sl)->getName() << "=" << juce::String (pl * 100.0f, 2) << "% ";
                    }
                    std::cerr << "MULTICORE rack " << e.getRackConfig (r).name << " load now " << l.now * 100.0f << "% peak " << l.peak * 100.0f
                              << "% | plugins " << slots << std::endl;
                }
                e.setMultiCore (true);
            });

        if (open == "selftest")
            juce::Timer::callAfterDelay (600, [&m] { selfTest (m); });
        if (open == "selftest2")
            juce::Timer::callAfterDelay (600, [&m] { selfTest2 (m); });

        if (open == "dragtest")
        {
            // Exercise the drag-and-drop logic without a mouse:
            m.handleSlotDrop (0, 2, 1, 0, false); // move Lead Vox's DSP demo to the top of Kick
            m.handleSlotDrop (2, 1, 2, 0, false); // reorder Snare: compressor before gate
            m.handleSlotDrop (1, 1, 3, 0, true);  // Option-drag: copy Kick's compressor to Drum Bus top
            m.select (2, 0);
        }
        if (open == "dual" || open == "dualfull")
        {
            if (open == "dualfull")
            {
                // Fill one rack to see how many slots a tall strip shows
                const auto types = m.engine.getKnownPlugins().getTypes();
                for (int i = 0; i < 12; ++i)
                {
                    juce::String error;
                    m.engine.addPlugin (5, types[i % types.size()], error);
                }
            }

            // Pretend two 1280-wide screens side by side
            m.getTopLevelComponent()->setBounds (0, 0, 1270, 900);
            m.setTwoWindowMode (true);
            m.rackWindow->setBounds (1290, 0, 1270, 900);
            m.select (0, 1);
        }
    }

    // Checks latency groups, snapshot scope / hot plugins / BPM round trip and OSC
    static void selfTest (MainComponent& m)
    {
        auto& e = m.engine;
        testSignal.reset();
        auto say = [] (const juce::String& s) { std::cerr << "SELFTEST " << s << std::endl; };

        // --- Latency groups: Keys (GainPlugin only) joins group A (Kick, Snare, Drum Bus: 3 ms compressors)
        e.setRackLatencyGroup (4, 1);
        e.timerCallback();
        for (int r = 0; r < e.getNumRacks(); ++r)
            say ("latency rack " + juce::String (r) + " " + e.getRackConfig (r).name + " group " + RackConfig::groupName (e.getRackConfig (r).latencyGroup)
                 + " own " + juce::String (e.getRackLatencySamples (r)) + " total ms " + juce::String (e.getRackTotalLatencyMs (r), 3));

        // Impulse through everything: where does it come out on Keys (out 7) and on Pastor Mic (out 9, ungrouped)?
        juce::AudioBuffer<float> in (16, 480), out (16, 480);
        e.setSlotBypassed (5, 0, true); // Pastor's gate would swallow the impulse
        in.clear();
        for (int flush = 0; flush < 400; ++flush)   // empty the delay lines of test tone
            e.previewProcess (in, out);
        int keysAt = -1, pastorAt = -1, n = 0;
        for (int block = 0; block < 4; ++block)
        {
            in.clear();
            if (block == 0) { in.setSample (6, 0, 1.0f); in.setSample (8, 0, 1.0f); }
            e.previewProcess (in, out);
            for (int i = 0; i < 480; ++i, ++n)
            {
                if (keysAt < 0 && std::abs (out.getSample (6, i)) > 0.001f) keysAt = n;
                if (pastorAt < 0 && std::abs (out.getSample (8, i)) > 0.001f) pastorAt = n;
            }
        }
        say ("impulse Keys (group A, padded) out at sample " + juce::String (keysAt) + " (expect 144)");
        say ("impulse Pastor Mic (ungrouped) out at sample " + juce::String (pastorAt) + " (expect 0)");

        // Bypassed rack in a group keeps the group's delay
        e.setRackBypassed (4, true);
        say ("Keys bypassed total ms " + juce::String (e.getRackTotalLatencyMs (4), 3) + " (expect 3.000)");
        e.setRackBypassed (4, false);

        // --- Second driver: move Keys there; it must be processed exactly once
        {
            const int d = e.addDriver();
            auto cfg = e.getRackConfig (4);
            cfg.driver = d;
            e.setRackConfig (4, cfg);
            e.setRackLatencyGroup (4, 0);
            e.previewPrepare (48000.0, 480);
            in.clear();
            for (int flush = 0; flush < 400; ++flush) e.previewProcess (in, out);
            in.clear();
            in.setSample (6, 0, 1.0f);
            e.previewProcess (in, out);
            say ("driver 2: " + juce::String (e.getNumDrivers()) + " drivers, Keys on driver " + juce::String (e.getRackConfig (4).driver + 1)
                 + ", impulse peak out " + juce::String (out.getMagnitude (6, 0, 480), 3) + " (expect 1 x GainPlugin gain, not doubled)");
            const float onTwo = out.getMagnitude (6, 0, 480);
            cfg.driver = 0;
            e.setRackConfig (4, cfg);
            e.previewPrepare (48000.0, 480);
            for (int flush = 0; flush < 400; ++flush) { in.clear(); e.previewProcess (in, out); }
            in.setSample (6, 0, 1.0f);
            e.previewProcess (in, out);
            say ("back on driver 1: level " + juce::String (out.getMagnitude (6, 0, 480), 3) + " vs " + juce::String (onTwo, 3)
                 + " group " + RackConfig::groupName (e.getRackConfig (4).latencyGroup)
                 + " totalms " + juce::String (e.getRackTotalLatencyMs (4), 3) + " byp " + juce::String ((int) e.isRackBypassed (4))
                 + " in " + juce::String (e.getRackConfig (4).input[0]) + " out " + juce::String (e.getRackConfig (4).output[0])
                 + " peakOut " + juce::String (e.consumePeak (4, true, 0), 3) + " peakIn " + juce::String (e.consumePeak (4, false, 0), 3));
            e.removeDriver (d);
            say ("driver removed: " + juce::String (e.getNumDrivers()) + " driver(s)");
        }

        // --- Show file round trip, with snapshots
        e.setBpm (96.5);
        auto scope = e.getScope();
        scope.rackBypass = false;
        e.setScope (scope);
        e.storeSnapshot (5, "Verse");
        e.storeSnapshot (2, "Intro");
        const auto hotBefore = e.getHotPlugins();
        const int idBefore = e.getSlotId (0, 1);
        auto xml = e.createShow();

        m.closeAllEditors();
        e.newShow();
        e.setBpm (120.0);
        juce::String error;
        const bool ok = e.loadShow (*xml, error);
        say ("full reload ok=" + juce::String ((int) ok) + " racks=" + juce::String (e.getNumRacks()) + " error='" + error + "'");
        say ("bpm after reload " + juce::String (e.getBpm(), 1) + " (expect 96.5)");
        say ("plugin id kept: " + juce::String ((int) (e.getSlotId (0, 1) == idBefore)));
        say ("hot plugins kept: " + juce::String ((int) (e.getHotPlugins() == hotBefore)) + " count " + juce::String ((int) e.getHotPlugins().size()));
        say ("scope kept: rackBypass=" + juce::String ((int) e.getScope().rackBypass) + " slot(0,0) inScope=" + juce::String ((int) e.isSlotInScope (0, 0)));
        say ("snapshots kept: " + juce::String ((int) e.getSnapshots().size()) + " first " + juce::String (e.getSnapshots()[0].number)
             + " " + e.getSnapshots()[0].name + " (expect 2 Intro), 5 is " + (e.getSnapshotInfo (5) ? e.getSnapshotInfo (5)->name : juce::String ("missing")));
        say ("Keys still group " + RackConfig::groupName (e.getRackConfig (4).latencyGroup));
        say ("second open is in-place=" + juce::String ((int) e.snapshotMatchesCurrentLayout (*xml)));

        // Snapshot recall follows scope: a plugin left out keeps its settings
        auto* gate = e.getPlugin (0, 0);   // Lead Vox gate: out of scope
        auto* comp = e.getPlugin (0, 1);   // Lead Vox compressor: in scope
        const float compBefore = comp->getParameters()[0]->getValue();
        gate->getParameters()[0]->setValueNotifyingHost (0.9f);
        comp->getParameters()[0]->setValueNotifyingHost (0.9f);
        e.setRackName (0, "Renamed");
        const bool rackBypBefore = e.isRackBypassed (6);
        e.setRackBypassed (6, ! rackBypBefore);
        e.setSlotBypassed (0, 1, true);
        const int racksBefore = e.getNumRacks();
        {
            juce::String err;
            e.addPlugin (1, e.getSlotDescription (0, 1), err); // new since the snapshot
        }
        juce::String note;
        const bool found = e.recallSnapshot (5, note);
        say ("recall 5 found=" + juce::String ((int) found)
             + "  gate (out of scope) kept 0.9: " + juce::String ((int) (std::abs (gate->getParameters()[0]->getValue() - 0.9f) < 0.01f))
             + "  comp (in scope) back to " + juce::String (compBefore, 2) + ": " + juce::String ((int) (std::abs (comp->getParameters()[0]->getValue() - compBefore) < 0.01f))
             + "  comp bypass back off: " + juce::String ((int) ! e.isSlotBypassed (0, 1))
             + "  name kept: " + e.getRackConfig (0).name
             + "  rack bypass left (not in scope): " + juce::String ((int) (e.isRackBypassed (6) != rackBypBefore))
             + "  racks unchanged: " + juce::String ((int) (e.getNumRacks() == racksBefore)));
        say ("recall note: '" + note + "' (expect 1 plugin isn't in this snapshot)");
        say ("recall 99 found=" + juce::String ((int) e.recallSnapshot (99, note)) + " (expect 0)");
        e.removePlugin (1, e.getNumSlots (1) - 1);
        e.setRackBypassed (6, rackBypBefore);

        // A pre-0.13 scene file (a full rig) imports as a snapshot
        {
            auto old = e.createSnapshot();
            juce::String err;
            const bool imported = e.importSceneFile (*old, 7, "Old", err);
            say ("scene file import ok=" + juce::String ((int) imported) + " err='" + err + "' now " + juce::String ((int) e.getSnapshots().size()) + " snapshots");
            e.deleteSnapshot (7);
            say ("after delete: " + juce::String ((int) e.getSnapshots().size()) + " snapshots");
        }

        // --- OSC: bypass rack 2 over the network, read the feedback
        juce::DatagramSocket client (false);
        client.bindToPort (0);
        juce::OSCSender sender;
        sender.connectToSocket (client, "127.0.0.1", m.osc.getPort());
        sender.send ("/liverack/rack/2/bypass", 1);
        juce::Timer::callAfterDelay (400, [&m, say, sock = &client]
        {
            say ("OSC: rack 2 bypassed = " + juce::String ((int) m.engine.isRackBypassed (1)) + ", clients " + juce::String (m.osc.getNumClients()));
            juce::ignoreUnused (sock);
        });

        static std::unique_ptr<juce::DatagramSocket> keep; // keeps the client alive for feedback
        keep = std::make_unique<juce::DatagramSocket> (false);
        keep->bindToPort (0);
        {
            juce::OSCSender keepSender;
            keepSender.connectToSocket (*keep, "127.0.0.1", m.osc.getPort());
            keepSender.send ("/liverack/subscribe");
        }

        juce::Timer::callAfterDelay (1200, [say]
        {
            char buf[4096];
            int got = 0;
            juce::StringArray addresses;
            while (keep->waitUntilReady (true, 50) > 0 && got < 400)
            {
                const int len = keep->read (buf, sizeof (buf), false);
                if (len <= 0) break;
                addresses.add (juce::String::fromUTF8 (buf));
                ++got;
            }
            say ("OSC feedback messages received: " + juce::String (got));
            for (auto& a : addresses)
                if (a.contains ("/rack/2/bypass") || a.contains ("/rack/1/name") || a == "/liverack/bpm" || a.contains ("/key/1/label"))
                    say ("  feedback: " + a);
            keep.reset();
        });
    }

    // 0.10: mono-to-stereo racks, pages, auto-patch, shortcuts
    static void selfTest2 (MainComponent& m)
    {
        auto& e = m.engine;
        testSignal.reset();
        auto say = [] (const juce::String& s) { std::cerr << "SELFTEST2 " << s << std::endl; };

        auto find = [&e] (const juce::String& name)
        {
            for (auto& d : e.getKnownPlugins().getTypes())
                if (d.name == name) return d;
            return juce::PluginDescription {};
        };

        // --- Mono-to-stereo rack: gate, gain, then the compressor as the stereo point
        RackConfig c;
        c.name = "Vox FX";
        c.monoToStereo = true;
        c.input[0] = 12; c.output[0] = 12; c.output[1] = 13;
        const int r = e.addRack (c);
        juce::String err;
        e.addPlugin (r, find ("NoiseGatePlugin"), err);
        e.addPlugin (r, find ("GainPlugin"), err);
        e.addPlugin (r, find ("DemoCompressor"), err);
        e.setStereoPoint (r, 2);
        e.previewPrepare (48000.0, 480);

        auto widths = [&e, r]
        {
            juce::String w;
            for (int s = 0; s < e.getNumSlots (r); ++s)
                w << e.getPlugin (r, s)->getName() << "=" << e.getPlugin (r, s)->getMainBusNumInputChannels() << ">"
                  << e.getSlotOutputChannels (r, s) << " ";
            return w;
        };
        say ("M>S chain: " + widths() + " stereo point " + juce::String (e.getStereoPoint (r)) + " (expect 2, last one 1>2)");

        e.setSlotBypassed (r, 0, true); // gate would close on a test tone
        juce::AudioBuffer<float> in (16, 480), out (16, 480);
        for (int k = 0; k < 20; ++k)
        {
            in.clear();
            for (int i = 0; i < 480; ++i) in.setSample (12, i, 0.25f * std::sin (0.05f * (float) (k * 480 + i)));
            e.previewProcess (in, out);
        }
        say ("M>S output L " + juce::String (out.getMagnitude (12, 0, 480), 3) + "  R " + juce::String (out.getMagnitude (13, 0, 480), 3)
             + " (both should carry the mono source)");

        // Move the compressor to the top: it stays the stereo point, the rest go stereo
        e.moveSlotTo (r, 2, 0);
        say ("after moving it to the top: " + widths() + " stereo point " + juce::String (e.getStereoPoint (r)));

        // --- Pages
        std::vector<RackPage> pages { { "Drums", { e.getRackId (1), e.getRackId (2), e.getRackId (3) } },
                                      { "Vocals", { e.getRackId (0), e.getRackId (r) } } };
        e.setPages (pages);
        e.setCurrentPage (1);
        juce::String vis;
        for (int v : e.getVisibleRacks()) vis << e.getRackConfig (v).name << ", ";
        say ("page Vocals shows: " + vis);

        const int drumId = e.getRackId (1);
        e.saveSession();
        m.closeAllEditors();
        e.clearAll();
        e.setPages ({});
        juce::String error;
        e.restoreSession (error);
        say ("after session reload: " + juce::String ((int) e.getPages().size()) + " pages, current " + juce::String (e.getCurrentPage())
             + ", Kick id kept " + juce::String ((int) (e.getRackId (1) == drumId))
             + ", Drums page has " + juce::String ((int) e.getPages()[0].rackIds.size()) + " racks"
             + ", Vox FX is M>S " + juce::String ((int) e.getRackConfig (e.getNumRacks() - 1).isMonoToStereo())
             + ", stereo point " + juce::String (e.getStereoPoint (e.getNumRacks() - 1)));
        m.refreshAll();

        // The show carries pages and snapshots
        auto show = e.createShow();
        say ("show has PAGES: " + juce::String ((int) (show->getChildByName ("PAGES") != nullptr))
             + " SNAPSHOTS: " + juce::String (show->getChildByName ("SNAPSHOTS") != nullptr ? show->getChildByName ("SNAPSHOTS")->getNumChildElements() : -1)
             + " USERKEYS: " + juce::String ((int) (show->getChildByName ("USERKEYS") != nullptr)));

        // --- Auto-patch: Pastor Mic (index 5) uses out 9; a new empty rack patched to input 9 must not take it
        RackConfig blank;
        blank.name = "New";
        blank.input[0] = blank.output[0] = -1;
        const int nr = e.addRack (blank);
        m.showRouting (nr);
        auto cfg = e.getRackConfig (nr);
        cfg.input[0] = 8;
        m.routingMatrix->autoPatchOutputs (nr, cfg, 0, 8);
        say ("auto-patch to taken out 9: output " + juce::String (cfg.output[0] + 1) + " (expect 0 = left unpatched), note: " + m.routingMatrix->note);
        cfg.input[0] = 14; cfg.output[0] = -1;
        m.routingMatrix->note.clear();
        m.routingMatrix->autoPatchOutputs (nr, cfg, 0, 14);
        say ("auto-patch to free out 15: output " + juce::String (cfg.output[0] + 1) + " (expect 15)");

        // --- Shortcuts
        say ("F1 is User Key " + juce::String (m.keysBar.keyForShortcut (juce::KeyPress (juce::KeyPress::F1Key)) + 1)
             + ", T is: " + m.shortcutOwner (juce::KeyPress ('t'))
             + ", Cmd+Q reserved: " + juce::String ((int) Shortcuts::isReserved (juce::KeyPress ('q', juce::ModifierKeys::commandModifier, 0))));
        say ("MIDI devices added: " + juce::String ((int) m.midi->getDevices().size()) + " (expect 0 to start)");
    }

    static void buildSampleSession (MainComponent& m, const juce::File& dir)
    {
        auto& engine = m.engine;

        for (auto* format : engine.getFormatManager().getFormats())
            for (auto& f : dir.findChildFiles (juce::File::findDirectories, true, "*.vst3"))
            {
                juce::OwnedArray<juce::PluginDescription> found;
                engine.getKnownPlugins().scanAndAddFile (f.getFullPathName(), true, found, *format);
            }

        auto find = [&engine] (const juce::String& name)
        {
            for (auto& d : engine.getKnownPlugins().getTypes())
                if (d.name == name)
                    return d;
            return juce::PluginDescription {};
        };

        const auto gate = find ("NoiseGatePlugin"), dsp = find ("DSPModulePluginDemo"), gain = find ("GainPlugin"),
                   comp = find ("DemoCompressor");

        struct RackDef { const char* name; bool stereo; int in, out, console; bool safe; int group; std::vector<juce::PluginDescription> plugins; };
        const std::vector<RackDef> defs {
            { "Lead Vox",   false, 0, 0,  1, false, 3, { gate, comp, dsp } },
            { "Kick",       false, 1, 1,  2, false, 1, { comp, gain } },
            { "Snare",      false, 2, 2,  3, false, 1, { gate, comp } },
            { "Drum Bus",   true,  4, 4,  0, false, 1, { comp, dsp } },
            { "Keys",       true,  6, 6,  9, true,  0, { gain } },
            { "Pastor Mic", false, 8, 8, 12, false, 0, { gate, comp, gain } },
            { "Choir",      true, 10, 10, 17, false, 3, { comp, gain } },
        };

        m.closeAllEditors();
        engine.clearAll();

        for (auto& d : defs)
        {
            RackConfig c;
            c.name = d.name; c.stereo = d.stereo; c.patchContiguous (d.in, d.out);
            c.consoleChannel = d.console; c.recallSafe = d.safe; c.latencyGroup = d.group;
            const int r = engine.addRack (c);

            for (auto& p : d.plugins)
            {
                juce::String error;
                engine.addPlugin (r, p, error);
            }
        }

        engine.setSlotBypassed (5, 2, true);
        engine.setRackBypassed (6, true);
        engine.setRackColour (1, 5); // Kick: red
        engine.setRackColour (2, 5); // Snare: red
        engine.setRackColour (3, 5); // Drum Bus: red
        engine.setRackColour (4, 3); // Keys: purple

        // Prepare the racks as if an audio device had started, then feed them
        engine.previewPrepare (48000.0, TestSignal::block);
        testSignal = std::make_unique<TestSignal> (engine);

        auto sceneDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("LiveRack Scenes");
        sceneDir.createDirectory();

        ConsoleLink::Settings s;
        s.type = ConsoleLink::Type::yamahaRcp;
        s.address = "192.168.0.128";
        s.sceneFolder = sceneDir;
        s.slots = ConsoleLink::defaultSlots (s.type);
        s.slots[1].card = 2; s.slots[1].numChannels = 64; s.slots[1].driver = 1;
        s.slots[2].card = 2; s.slots[2].numChannels = 64; s.slots[2].driver = 1; s.slots[2].firstChannel = 65;
        s.name = "FOH CL5";
        m.consoles[0]->setSettings (s);

        engine.pinHot (engine.getSlotId (0, 1));
        engine.pinHot (engine.getSlotId (0, 2));
        engine.pinHot (engine.getSlotId (1, 0));
        engine.pinHot (engine.getSlotId (2, 0));
        engine.pinHot (engine.getSlotId (5, 1));
        engine.setSlotInScope (0, 0, false);
        engine.setSlotInScope (3, 1, false);
        m.currentScene = 12;

        m.refreshPluginList();
        m.select (0, 1);
    }
};
