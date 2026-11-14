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

        at (13500, c, [] (ClientComponent&, View& v) { v.setView (View::View::routing); std::cerr << "CLIENT routing " << (v.routingMatrix != nullptr) << std::endl; });
        at (14500, c, [] (ClientComponent&, View& v) { v.setView (View::View::snapshots); std::cerr << "CLIENT snapshots " << (v.snapshotsPanel != nullptr) << std::endl; });
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
