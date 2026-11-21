#pragma once
// Linux preview builds only (LIVERACK_PREVIEW_HOOKS): drives LiveRack Client
// against a real server process and prints what it sees, for testing.
// LIVERACK_PREVIEW_CLIENT=basic

struct ClientPreview
{
    using View = ClientComponent::ClientView;

    static juce::MouseEvent mouseAt (juce::Component& c, juce::Point<int> p, int clicks, juce::ModifierKeys mods = {})
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p.toFloat(), mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &c, &c, now, p.toFloat(), now, clicks, false);
    }

    static void at (int ms, ClientComponent& c, std::function<void (ClientComponent&, View&)> fn)
    {
        juce::Component::SafePointer<ClientComponent> safe (&c);
        juce::Timer::callAfterDelay (ms, [safe, fn]
        {
            if (safe != nullptr && safe->view != nullptr)
                fn (*safe, *safe->view);
            else if (safe != nullptr)
                std::cerr << "CLIENT no view (connect screen)" << std::endl;
        });
    }

    static void run (ClientComponent& c)
    {
        if (juce::SystemStats::getEnvironmentVariable ("LIVERACK_PREVIEW_OPEN", {}) == "switchmode")
        {
            std::cerr << "CLIENT up: connect screen " << (c.connectScreen != nullptr) << " mode client " << AppMode::isClient << std::endl;
            juce::Timer::callAfterDelay (2500, [] { if (AppMode::switchTo != nullptr) AppMode::switchTo (false); });
            return;
        }

        const auto test = juce::SystemStats::getEnvironmentVariable ("LIVERACK_PREVIEW_CLIENT", {});
        if (test.isEmpty())
            return;

        if (test == "controls")
        {
            at (4000, c, [] (ClientComponent&, View& v)
            {
                auto& l = v.live;
                std::cerr << "CLIENT connected " << l.isConnected() << " midi " << (v.midi != nullptr) << " key shortcut 0 '" << Shortcuts::describe (v.keyShortcuts[0]) << "'" << std::endl;

                // Assign User Key 1 on the server: select rack 3
                Action sel; sel.type = Action::selectRack; sel.rack = 2;
                v.assignKey (0, sel);

                // MIDI Learn here: a note bypasses rack 2, a CC sets parameter 0 of rack 1's first plugin
                Action bypass; bypass.type = Action::rackBypass; bypass.rack = 1;
                v.midi->learnAction (bypass);
                v.midi->handle ({ "Test pad", juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) });
                std::cerr << "CLIENT learned " << v.midi->getMappings().size() << " '" << v.midi->getMappings().back().describeControl()
                          << "' -> '" << v.midi->getMappings().back().describeTarget (l) << "' rack1 bypassed before " << l.isRackBypassed (1) << std::endl;
                v.midi->handle ({ "Test pad", juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) });

                v.midi->learnParameter (0, 0, 0, false, "First parameter");
                v.midi->handle ({ "Test pad", juce::MidiMessage::controllerEvent (1, 20, 10) });
                // a fader: many values in one tick, only the last counts
                {
                    const juce::ScopedLock sl (v.midi->queueLock);
                    for (int val : { 20, 40, 60, 96 })
                        v.midi->queue.push_back ({ "Test pad", juce::MidiMessage::controllerEvent (1, 20, val) });
                }
                std::cerr << "CLIENT mappings now " << v.midi->getMappings().size() << std::endl;

                // Server apps: give the server one
                auto ss = l.getServerSettings();
                l.setServerSettings (ss.startAtLogin, ss.restartAfterCrash, ss.multiCore, juce::StringArray { "/usr/bin/true" });
            });

            at (6500, c, [] (ClientComponent&, View& v)
            {
                auto& l = v.live;
                std::cerr << "CLIENT rack1 bypassed after the note " << l.isRackBypassed (1) << " key0 label '" << v.state()["keys"][0]["label"].toString()
                          << "' apps " << v.state()["apps"].size() << " apps button " << v.appsButton.isVisible() << std::endl;

                // F1 (this computer's shortcut for User Key 1) selects rack 3
                v.keyPressed (juce::KeyPress (juce::KeyPress::F1Key), &v);
                std::cerr << "CLIENT after F1 selected " << v.selectedRack << " (expect 2)" << std::endl;

                // The parameter the fader set
                auto* extra = new juce::DynamicObject();
                extra->setProperty ("view", "0:0");
                juce::Component::SafePointer<ClientComponent> sc (&v.owner);
                l.request ("params", juce::var (extra), [] (const juce::var& d)
                {
                    const auto p0 = d["view"]["params"][0];
                    std::cerr << "CLIENT param 0 '" << p0["name"].toString() << "' value " << (double) p0["value"] << " (expect " << 96.0 / 127.0 << ")" << std::endl;
                });

                // Recalled by new snapshots, as the plugin menu asks
                auto* e2 = new juce::DynamicObject();
                e2->setProperty ("number", -1);
                l.request ("snapshot", juce::var (e2), [] (const juce::var& d)
                {
                    std::cerr << "CLIENT new snapshots recall rack 0: " << juce::JSON::toString (d["snapshot"]["inScope"][0], true) << std::endl;
                });

                v.setTwoWindows (true);
                v.openApp (0, "true");
            });

            at (10000, c, [] (ClientComponent&, View& v)
            {
                auto* pic = v.dock.remoteView.get();
                std::cerr << "CLIENT rack window " << (v.rackWindow != nullptr) << " dock in it " << (v.dock.getParentComponent() != &v)
                          << " picture " << (pic != nullptr && pic->picture.isValid()) << " view " << (int) v.view
                          << " pop outs (app) " << v.popOuts.size() << " app message '" << [&] {
                                 auto* w = v.popOuts.empty() ? nullptr : dynamic_cast<RemotePluginView*> (v.popOuts.front()->getContentComponent());
                                 return w != nullptr ? w->message : juce::String ("-"); }() << "'" << std::endl;
                std::cerr << "CLIENT status learn '" << v.midi->getLearnText() << "'" << std::endl;
                v.setTwoWindows (false);
                std::cerr << "CLIENT back to one window: dock here " << (v.dock.getParentComponent() == &v) << " rack window " << (v.rackWindow != nullptr) << std::endl;
                v.setView (View::View::devices);
                std::cerr << "CLIENT devices with MIDI: cards " << (v.devicesPanel != nullptr) << std::endl;
            });
            at (12000, c, [] (ClientComponent&, View& v)
            {
                v.setView (View::View::channels);
                std::cerr << "CLIENT isolate all before: " << (bool) v.state()["isolateAll"] << " (expect 0)" << std::endl;
                v.send (makeCmd ("isolateAll", { { "on", true } })); // what the show menu item sends
            });
            at (13500, c, [] (ClientComponent&, View& v)
            {
                std::cerr << "CLIENT isolate all after: " << (bool) v.state()["isolateAll"] << " (expect 1)" << std::endl;
                v.send (makeCmd ("isolateAll", { { "on", false } }));
            });
            return;
        }

        if (test == "clickserver")
        {
            // As a person would: wait for the server to show up, click it
            juce::Component::SafePointer<ClientComponent> safe (&c);
            auto hasServerButton = [] (ClientComponent& cc)
            {
                for (auto* child : cc.connectScreen->getChildren())
                    if (auto* b = dynamic_cast<juce::TextButton*> (child); b != nullptr && b->getButtonText().contains ("LiveRack 0."))
                        return true;
                return false;
            };
            juce::Timer::callAfterDelay (3300, [safe, hasServerButton]
            {
                // No broadcast heard (some CI runners don't loop it back): the
                // server as a recent one instead, so there's a button to click
                if (safe != nullptr && safe->connectScreen != nullptr && ! hasServerButton (*safe))
                {
                    std::cerr << "CLIENT no broadcast heard: using a recent server" << std::endl;
                    rememberServer ("Test server", "127.0.0.1", 8090);
                    safe->showConnectScreen();
                }
            });
            juce::Timer::callAfterDelay (4000, [safe]
            {
                if (safe == nullptr || safe->connectScreen == nullptr) { std::cerr << "CLIENT no connect screen" << std::endl; return; }
                for (auto* child : safe->connectScreen->getChildren())
                    if (auto* b = dynamic_cast<juce::TextButton*> (child); b != nullptr
                        && (b->getButtonText().contains ("LiveRack 0.") || b->getButtonText().contains ("127.0.0.1")))
                    {
                        std::cerr << "CLIENT clicking '" << b->getButtonText() << "'" << std::endl;
                        b->triggerClick();
                        break;
                    }
            });
            juce::Timer::callAfterDelay (8000, [safe]
            {
                if (safe == nullptr) return;
                // A typed address with the wrong port, then nothing at all there
                if (safe->view != nullptr)
                    std::cerr << "CLIENT problem now '" << safe->view->live.getLastProblem() << "'" << std::endl;
                std::cerr << "CLIENT after click: view " << (safe->view != nullptr) << " connected "
                          << (safe->view != nullptr && safe->view->live.isConnected()) << " racks "
                          << (safe->view != nullptr ? safe->view->live.getNumRacks() : -1) << std::endl;
            });
            return;
        }

        at (4000, c, [] (ClientComponent&, View& v)
        {
            auto& l = v.live;
            std::cerr << "CLIENT connected " << l.isConnected() << " server '" << l.getServerName() << "' version " << l.getServerVersion()
                      << " racks " << l.getNumRacks() << " families " << v.library.getFamilies().size()
                      << " overlay " << v.overlay->isVisible() << std::endl;
            for (int r = 0; r < l.getNumRacks(); ++r)
            {
                std::cerr << "CLIENT rack " << r << " '" << l.getRackConfig (r).name << "' slots";
                for (int s = 0; s < l.getNumSlots (r); ++s)
                    std::cerr << " [" << l.getSlot (r, s).name << "]";
                std::cerr << std::endl;
            }
            const auto title = v.getTopLevelComponent() != nullptr ? v.getTopLevelComponent()->getName() : juce::String();
            std::cerr << "CLIENT title '" << title << "'" << std::endl;

            // Open the first plugin in Rack: its window as a picture
            v.strips.onSelect (0, 1);
            v.setView (View::View::rack);
        });

        at (7000, c, [] (ClientComponent&, View& v)
        {
            auto* pic = v.dock.remoteView.get();
            std::cerr << "CLIENT picture " << (pic != nullptr) << " size " << (pic != nullptr ? pic->getNaturalSize().toString() : juce::String())
                      << " valid " << (pic != nullptr && pic->picture.isValid()) << " message '" << (pic != nullptr ? pic->message : juce::String()) << "'"
                      << " dock pref height " << v.dock.getPreferredHeight() << std::endl;

            if (pic != nullptr && pic->picture.isValid())
            {
                juce::File f ("/tmp/client-frame.png");
                f.deleteFile();
                juce::FileOutputStream out (f);
                juce::PNGImageFormat().writeImageToStream (pic->picture, out);
                const auto px = pic->picture.getPixelAt (pic->picture.getWidth() / 2, 20);
                std::cerr << "CLIENT frame " << pic->picture.getWidth() << "x" << pic->picture.getHeight() << " format " << (int) pic->picture.getFormat()
                          << " pixel " << px.toDisplayString (true) << std::endl;
            }

            // Drag in the picture (a slider in the generic editor), as a mouse would
            if (pic != nullptr && pic->getWidth() > 0)
            {
                const auto area = pic->pictureArea();
                const juce::Point<int> from ((int) (area.getX() + area.getWidth() * 0.5f), (int) (area.getY() + 20));
                pic->mouseDown (mouseAt (*pic, from, 1));
                for (int i = 1; i <= 10; ++i)
                    pic->mouseDrag (mouseAt (*pic, from + juce::Point<int> (i * 12, 0), 1));
                pic->timerCallback();
                pic->mouseUp (mouseAt (*pic, from + juce::Point<int> (120, 0), 1));
            }
        });

        // Changes from here reach the server, and come back
        at (9500, c, [] (ClientComponent&, View& v)
        {
            auto& l = v.live;
            Action bypass; bypass.type = Action::rackBypass; bypass.rack = 1;
            v.performAction (bypass, -1);
            v.send (makeCmd ("bpm", { { "value", 133.0 } }));
            v.send (makeCmd ("sceneStore", { { "number", 7 }, { "name", "From the client" } }));
            v.selectNewRack = true;
            v.send (makeCmd ("addRack"));
            if (! v.library.getFamilies().empty())
                v.send (makeCmd ("addPlugin", { { "rack", 0 }, { "key", v.library.getFamilies().front().key } }));
            auto pages = l.getPages();
            RackPage p; p.name = "Client page"; p.rackIds.push_back (l.getRackConfig (0).id);
            pages.push_back (p);
            l.setPages (pages);
            std::cerr << "CLIENT before: racks " << l.getNumRacks() << " rack0 slots " << l.getNumSlots (0) << std::endl;
        });

        at (12000, c, [] (ClientComponent&, View& v)
        {
            auto& l = v.live;
            std::cerr << "CLIENT after: racks " << l.getNumRacks() << " rack0 slots " << l.getNumSlots (0) << " rack1 bypassed " << l.isRackBypassed (1)
                      << " bpm " << v.bpm() << " snapshot7 " << l.hasSnapshot (7) << " '" << l.getSnapshotName (7) << "'"
                      << " pages " << l.getPages().size() << " selected " << v.selectedRack << " scene " << v.currentScene() << std::endl;
            const auto s = l.getSlot (0, l.getNumSlots (0) - 1);
            std::cerr << "CLIENT added plugin '" << s.name << "'" << std::endl;

            // Each screen opens and closes
            v.setView (View::View::devices);
            std::cerr << "CLIENT devices " << (v.devicesPanel != nullptr) << std::endl;
        });

        // The plugin list arriving while the picker is open (a scan finishing on the server)
        // must not rebuild the list under the picker; and a plugin the server doesn't know is refused
        static int slotsBeforeUnknown = -1;
        at (12500, c, [] (ClientComponent&, View& v)
        {
            const auto before = v.library.getFamilies().size();
            v.showAddPluginMenu (0);
            v.applyLibrary ({});
            std::cerr << "CLIENT list while picker open: picker " << (v.pickerOverlay != nullptr) << " families " << v.library.getFamilies().size()
                      << " (expect " << before << ")" << std::endl;
            v.pickerOverlay.reset();
            v.applyLibrary ({});
            std::cerr << "CLIENT list after picker closed: families " << v.library.getFamilies().size() << " (expect 0)" << std::endl;
            v.loadLibrary();   // back to the server's list
            slotsBeforeUnknown = v.live.getNumSlots (0);
            v.send (makeCmd ("addPlugin", { { "rack", 0 }, { "key", "no such plugin" } }));
            v.send (makeCmd ("addPlugin", { { "rack", 0 }, { "id", "id:no such plugin" } }));
        });
        at (13000, c, [] (ClientComponent&, View& v)
        {
            std::cerr << "CLIENT unknown plugin: slots " << v.live.getNumSlots (0) << " (expect " << slotsBeforeUnknown << ") families "
                      << v.library.getFamilies().size() << " (expect 4)" << std::endl;
        });

        at (13500, c, [] (ClientComponent&, View& v) { v.setView (View::View::routing); std::cerr << "CLIENT routing " << (v.routingMatrix != nullptr) << std::endl; });
        at (14500, c, [] (ClientComponent&, View& v) { v.setView (View::View::snapshots); std::cerr << "CLIENT snapshots " << (v.snapshotsPanel != nullptr) << std::endl; });
        // MIDI Learn on the Snapshots screen: a note on this computer recalls snapshot 7 on the server
        at (15000, c, [] (ClientComponent&, View& v)
        {
            if (v.snapshotsPanel == nullptr || v.midi == nullptr) { std::cerr << "CLIENT snapshot learn: no panel" << std::endl; return; }
            v.snapshotsPanel->select (7);
            juce::TextButton* learn = nullptr;
            for (auto* ch : v.snapshotsPanel->getChildren())
                if (auto* b = dynamic_cast<juce::TextButton*> (ch))
                    if (b->getButtonText().startsWith ("MIDI Learn"))
                        learn = b;
            if (learn != nullptr) learn->triggerClick();
            std::cerr << "CLIENT snapshot learn: button " << (learn != nullptr) << std::endl;
        });
        at (15150, c, [] (ClientComponent&, View& v)
        {
            if (v.midi == nullptr) return;
            std::cerr << "CLIENT snapshot learning " << v.midi->isLearning() << std::endl;
            v.midi->handle ({ "Test pad", juce::MidiMessage::noteOn (1, 62, (juce::uint8) 100) });
            std::cerr << "CLIENT snapshot learned: " << v.midi->getMappings().size() << " mapping(s), snapshot 7 trigger '" << v.midi->describeTriggerFor (7) << "'" << std::endl;
            v.midi->handle ({ "Test pad", juce::MidiMessage::noteOn (1, 62, (juce::uint8) 100) });
        });
        at (15300, c, [] (ClientComponent&, View& v)
        {
            std::cerr << "CLIENT snapshot note recalled: scene " << v.currentScene() << " (expect 7)" << std::endl;
        });
        at (15500, c, [] (ClientComponent&, View& v) { v.setView (View::View::plugins); std::cerr << "CLIENT plugins " << (v.tool != nullptr) << std::endl; });
        at (16500, c, [] (ClientComponent&, View& v)
        {
            v.setView (View::View::channels);
            v.popOut (0, 1);
            std::cerr << "CLIENT pop outs " << v.popOuts.size() << " picture in Rack gone " << (v.dock.remoteView == nullptr) << std::endl;
        });
        at (19500, c, [] (ClientComponent&, View& v)
        {
            auto* w = v.popOuts.empty() ? nullptr : v.popOuts.front().get();
            auto* pic = w != nullptr ? dynamic_cast<RemotePluginView*> (w->getContentComponent()) : nullptr;
            std::cerr << "CLIENT pop out window " << (w != nullptr ? w->getName() : juce::String()) << " picture valid "
                      << (pic != nullptr && pic->picture.isValid()) << " size " << (w != nullptr ? w->getBounds().toString() : juce::String()) << std::endl;
            if (w != nullptr)
                w->closeButtonPressed();
        });
        at (20500, c, [] (ClientComponent&, View& v) { std::cerr << "CLIENT pop outs after closing " << v.popOuts.size() << std::endl; });

        // The script stops the server at 22 s and starts it again at 26 s
        at (24500, c, [] (ClientComponent&, View& v)
        {
            std::cerr << "CLIENT server gone: connected " << v.live.isConnected() << " overlay " << v.overlay->isVisible() << std::endl;
        });
        at (34000, c, [] (ClientComponent&, View& v)
        {
            std::cerr << "CLIENT server back: connected " << v.live.isConnected() << " overlay " << v.overlay->isVisible()
                      << " racks " << v.live.getNumRacks() << std::endl;
        });
        juce::Component::SafePointer<ClientComponent> safe (&c);
        juce::Timer::callAfterDelay (35000, [safe]
        {
            if (safe == nullptr) return;
            safe->showConnectScreen();
        });
        juce::Timer::callAfterDelay (40000, [safe]
        {
            if (safe == nullptr || safe->finder == nullptr) return;
            std::cerr << "CLIENT connect screen: listening " << safe->finder->isListening() << " servers";
            for (auto& s : safe->finder->getServers())
                std::cerr << " [" << s.name << " " << s.host << ":" << s.port << " " << s.version << "]";
            std::cerr << " recent " << loadRecentServers().size() << std::endl;
        });
    }
};
