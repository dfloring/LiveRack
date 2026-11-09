#pragma once
// Only compiled into Linux preview builds (LIVERACK_PREVIEW_HOOKS=1), used to
// set up a sample session, feed a test signal through it, and open dialogs
// for UI screenshots. Never part of the Mac app.
#include "MainComponent.h"

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
