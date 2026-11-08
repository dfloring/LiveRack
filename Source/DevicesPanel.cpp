#include "DevicesPanel.h"
#include "RackStripsView.h"
#include "Theme.h"

namespace
{
    void fillCard (juce::Graphics& g, juce::Rectangle<int> r)
    {
        g.setColour (Theme::panel);
        g.fillRoundedRectangle (r.toFloat(), 12.0f);
        g.setColour (Theme::border);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 12.0f, 1.0f);
    }

    void statTile (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& key, const juce::String& value)
    {
        g.setColour (Theme::raised);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        auto t = r.reduced (10, 6);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (key, t.removeFromTop (14), juce::Justification::centredLeft);
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        g.drawFittedText (value, t, juce::Justification::centredLeft, 1, 0.7f);
    }

    juce::String ago (juce::uint32 ms)
    {
        if (ms == 0) return "never";
        const auto d = (juce::Time::getMillisecondCounter() - ms) / 1000;
        if (d < 1)  return "just now";
        if (d < 60) return juce::String ((int) d) + " s ago";
        return juce::String ((int) (d / 60)) + " min ago";
    }

    bool recent (juce::uint32 ms, juce::uint32 within = 1500)
    {
        return ms != 0 && juce::Time::getMillisecondCounter() - ms < within;
    }

    juce::String bufferMs (const DriverStatus& s)
    {
        return s.sampleRate > 0 ? juce::String (1000.0 * s.bufferSize / s.sampleRate, 1) + " ms" : juce::String();
    }
}

//==============================================================================
class DevicesPanel::DriverCard : public juce::Component
{
public:
    DriverCard (Host& h, int i) : host (h), index (i)
    {
        addAndMakeVisible (settings);
        settings.onClick = [this] { if (host.openDriverSettings) host.openDriverSettings (index); };

        if (index > 0)
        {
            addAndMakeVisible (remove);
            remove.onClick = [this]
            {
                juce::Component::SafePointer<DriverCard> safe (this);
                juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Remove driver",
                    "Its racks move to driver 1.", "Remove", "Cancel", nullptr,
                    juce::ModalCallbackFunction::create ([safe] (int r)
                    {
                        if (safe != nullptr && r == 1)
                        {
                            auto& eng = safe->host.engine;
                            const int idx = safe->index;
                            juce::MessageManager::callAsync ([&eng, idx] { eng.removeDriver (idx); });
                        }
                    }));
            };
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        fillCard (g, area);
        area = area.reduced (16);

        const auto s = host.engine.getDriverStatus (index);
        const bool trouble = s.dropouts > 0 && juce::Time::getCurrentTime() - s.lastDropout < juce::RelativeTime::minutes (5);
        const auto light = ! s.open ? Theme::red : trouble ? Theme::amber : Theme::green;

        auto top = area.removeFromTop (28);
        Theme::drawLight (g, top.removeFromLeft (14).withSizeKeepingCentre (12, 12).toFloat(), light);
        top.removeFromLeft (10);

        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("DRIVER " + juce::String (index + 1), top.removeFromRight (70), juce::Justification::centredRight);

        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawFittedText (s.open ? s.deviceName : "No device: open Settings", top, juce::Justification::centredLeft, 1, 0.8f);

        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (s.typeName, area.removeFromTop (18), juce::Justification::centredLeft);
        area.removeFromTop (8);

        auto tiles = area.removeFromTop (52);
        const int tw = (tiles.getWidth() - 3 * 8) / 4;
        juce::String drops = s.dropouts < 0 ? juce::String ("n/a") : juce::String (s.dropouts);
        if (s.dropouts > 0)
            drops << "  " << s.lastDropout.toString (false, true, false, true);

        statTile (g, tiles.removeFromLeft (tw), "RATE", s.open ? juce::String (s.sampleRate / 1000.0, 1) + "k" : "-"); tiles.removeFromLeft (8);
        statTile (g, tiles.removeFromLeft (tw), "BUFFER", s.open ? juce::String (s.bufferSize) + "  " + bufferMs (s) : "-"); tiles.removeFromLeft (8);
        statTile (g, tiles.removeFromLeft (tw), "IN / OUT", s.open ? juce::String (s.numIns) + "/" + juce::String (s.numOuts) : "-"); tiles.removeFromLeft (8);
        statTile (g, tiles, "DROPOUTS", drops);

        area.removeFromTop (12);
        auto dspRow = area.removeFromTop (16);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText ("DSP", dspRow, juce::Justification::centredLeft);
        g.drawText (juce::String (s.cpu * 100.0, 0) + "%  (peak " + juce::String (s.peakCpu * 100.0, 0) + "%)", dspRow, juce::Justification::centredRight);
        auto bar = area.removeFromTop (8).toFloat();
        g.setColour (Theme::raised);
        g.fillRoundedRectangle (bar, 4.0f);
        g.setColour (s.peakCpu > 0.8 ? Theme::red : s.peakCpu > 0.6 ? Theme::amber : Theme::green);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, s.cpu)), 4.0f);

        area.removeFromTop (12);

        // Rack chips
        auto chips = area.removeFromTop (28);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        int shown = 0;
        for (int r = 0; r < host.engine.getNumRacks(); ++r)
        {
            if (host.engine.getRackConfig (r).driver != index && ! (index == 0 && host.engine.getRackConfig (r).driver >= host.engine.getNumDrivers()))
                continue;

            const auto name = host.engine.getRackConfig (r).name;
            const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name) + 16;
            if (w > chips.getWidth())
            {
                g.setColour (Theme::textDim);
                g.drawText ("+ more", chips, juce::Justification::centredLeft);
                break;
            }

            const auto c = RackStripsView::colourFor (host.engine, r);
            auto chip = chips.removeFromLeft (w).toFloat();
            chips.removeFromLeft (6);
            g.setColour (c.withAlpha (0.22f));
            g.fillRoundedRectangle (chip, 6.0f);
            g.setColour (c.withAlpha (0.7f));
            g.drawRoundedRectangle (chip.reduced (0.5f), 6.0f, 1.0f);
            g.setColour (Theme::text);
            g.drawText (name, chip.toNearestInt(), juce::Justification::centred);
            ++shown;
        }

        if (shown == 0)
        {
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText ("No racks on this driver yet. Patch racks to it in Routing.", chips, juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (16).removeFromBottom (40);
        settings.setBounds (b.removeFromRight (110));
        b.removeFromRight (8);
        remove.setBounds (b.removeFromRight (100));
    }

    Host& host;
    const int index;
    juce::TextButton settings { "Settings" }, remove { "Remove" };
};

//==============================================================================
class DevicesPanel::ConsoleCard : public juce::Component
{
public:
    ConsoleCard (Host& h, int i) : host (h), index (i)
    {
        for (auto* b : { &settings, &remove, &recallPill, &followPill })
            addAndMakeVisible (b);

        settings.onClick = [this] { if (host.openConsoleSettings) host.openConsoleSettings (index); };
        remove.onClick = [this]
        {
            if (host.removeConsole)
            {
                auto& hh = host;
                const int idx = index;
                juce::MessageManager::callAsync ([&hh, idx] { hh.removeConsole (idx); });
            }
        };

        recallPill.onClick = [this]
        {
            auto s = link().getSettings();
            s.recallScenes = ! s.recallScenes;
            link().setSettings (s);
            changed();
        };
        followPill.onClick = [this]
        {
            auto s = link().getSettings();
            s.followSelection = ! s.followSelection;
            link().setSettings (s);
            changed();
        };
    }

    ConsoleLink& link() { return *host.consoles[(size_t) index]; }
    void changed() { if (host.consolesChanged) host.consolesChanged(); repaint(); }

    juce::Rectangle<int> slotArea() const { return getLocalBounds().reduced (16).withTrimmedTop (56).withHeight (86); }

    juce::Rectangle<int> slotBounds (int i) const
    {
        const int n = juce::jmax (1, (int) host.consoles[(size_t) index]->getSettings().slots.size());
        auto a = slotArea();
        const int w = (a.getWidth() - (n - 1) * 8) / n;
        return { a.getX() + i * (w + 8), a.getY(), w, a.getHeight() };
    }

    void paint (juce::Graphics& g) override
    {
        auto& l = link();
        const auto& s = l.getSettings();
        auto area = getLocalBounds();
        fillCard (g, area);
        area = area.reduced (16);

        const bool off = s.type == ConsoleLink::Type::off;
        auto top = area.removeFromTop (30);
        Theme::drawLight (g, top.removeFromLeft (16).withSizeKeepingCentre (14, 14).toFloat(),
                          off ? Theme::textDim : l.isConnected() ? Theme::green : Theme::red);
        top.removeFromLeft (10);

        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        const auto name = l.getDisplayName();
        g.drawText (name, top, juce::Justification::centredLeft);
        const int nameW = juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (18.0f, juce::Font::bold), name);

        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (13.0f));
        juce::String sub = off ? "Link off: open Settings to choose the console"
                               : s.address + "  " + (s.type == ConsoleLink::Type::yamahaRcp ? "RCP" : "OSC")
                                 + (l.isConnected() ? "  connected" : "  not connected");
        g.drawText (sub, top.withTrimmedLeft (nameW + 14), juce::Justification::centredLeft, true);

        // Card slots
        for (int i = 0; i < (int) s.slots.size(); ++i)
        {
            const auto& c = s.slots[(size_t) i];
            auto r = slotBounds (i).toFloat();
            const auto tint = c.card == 1 ? Theme::accent : c.card == 2 ? Theme::amber : Theme::border;

            if (c.card == 0)
            {
                juce::Path outline, dashed;
                outline.addRoundedRectangle (r.reduced (0.5f), 10.0f);
                const float dashes[] = { 5.0f, 4.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
                g.setColour (Theme::border.brighter (0.2f));
                g.fillPath (dashed);
            }
            else
            {
                g.setColour (tint.withAlpha (0.14f));
                g.fillRoundedRectangle (r, 10.0f);
                g.setColour (tint);
                g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.2f);
            }

            auto t = r.reduced (12.0f, 8.0f).toNearestInt();
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText (c.slot.toUpperCase(), t.removeFromTop (14), juce::Justification::centredLeft, true);
            g.setColour (c.card == 0 ? Theme::textDim : Theme::text);
            g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
            g.drawFittedText (ConsoleLink::cardTypeName (c.card), t.removeFromTop (24), juce::Justification::centredLeft, 1, 0.8f);
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (12.0f));

            juce::String route = "Not used";
            if (c.card != 0 && c.numChannels > 0)
                route = host.engine.getDriverLabel (c.driver).substring (0, 18) + "  "
                        + juce::String (c.firstChannel) + "-" + juce::String (c.firstChannel + c.numChannels - 1);
            g.drawFittedText (route, t, juce::Justification::topLeft, 2, 0.8f);
        }

        // Activity
        area = getLocalBounds().reduced (16);
        area.removeFromTop (56 + 86 + 12);
        auto tiles = area.removeFromTop (56);
        const int tw = (tiles.getWidth() - 16) / 3;

        juce::String sel = "-";
        if (l.getLastSelectedChannel() > 0)
        {
            sel = "CH " + juce::String (l.getLastSelectedChannel());
            if (host.rackForChannel)
                if (auto rack = host.rackForChannel (index, l.getLastSelectedChannel()); rack.isNotEmpty())
                    sel << "  >  " << rack;
        }

        auto tile = [&g] (juce::Rectangle<int> r, const juce::String& k, const juce::String& v, juce::Colour light)
        {
            g.setColour (Theme::raised);
            g.fillRoundedRectangle (r.toFloat(), 8.0f);
            auto t = r.reduced (10, 6);
            Theme::drawLight (g, t.removeFromLeft (10).withSizeKeepingCentre (9, 9).toFloat(), light, false);
            t.removeFromLeft (8);
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText (k, t.removeFromTop (16), juce::Justification::centredLeft);
            g.setColour (Theme::text);
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.drawFittedText (v, t, juce::Justification::centredLeft, 1, 0.75f);
        };

        const bool flash = recent (l.getLastActivityMs(), 400);
        tile (tiles.removeFromLeft (tw), "LAST SCENE", l.getCurrentScene() >= 0 ? juce::String (l.getCurrentScene()) : juce::String ("-"), Theme::green.withAlpha (l.getCurrentScene() >= 0 ? 1.0f : 0.3f));
        tiles.removeFromLeft (8);
        tile (tiles.removeFromLeft (tw), "SELECTED", sel, Theme::accent.withAlpha (l.getLastSelectedChannel() > 0 ? 1.0f : 0.3f));
        tiles.removeFromLeft (8);
        tile (tiles, "LAST MESSAGE", ago (l.getLastActivityMs()), flash ? Theme::green : Theme::textDim.withAlpha (0.4f));
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (16).removeFromBottom (40);
        settings.setBounds (b.removeFromRight (110));
        b.removeFromRight (8);
        remove.setBounds (b.removeFromRight (100));
        recallPill.setBounds (b.removeFromLeft (140));
        b.removeFromLeft (8);
        followPill.setBounds (b.removeFromLeft (150));
    }

    void updatePills()
    {
        recallPill.setToggleState (link().getSettings().recallScenes, juce::dontSendNotification);
        followPill.setToggleState (link().getSettings().followSelection, juce::dontSendNotification);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int n = (int) link().getSettings().slots.size();
        for (int i = 0; i < n; ++i)
            if (slotBounds (i).contains (e.getPosition()))
            {
                showSlotMenu (i);
                return;
            }
    }

    void showSlotMenu (int i)
    {
        const auto c = link().getSettings().slots[(size_t) i];
        juce::PopupMenu m, card, drv, first, count;

        for (int t = 0; t < ConsoleLink::numCardTypes; ++t)
            card.addItem (100 + t, ConsoleLink::cardTypeName (t), true, c.card == t);
        for (int d = 0; d < host.engine.getNumDrivers(); ++d)
            drv.addItem (200 + d, host.engine.getDriverLabel (d), true, c.driver == d);
        for (int f : { 1, 9, 17, 25, 33, 41, 49, 57, 65, 97 })
            first.addItem (300 + f, juce::String (f), true, c.firstChannel == f);
        for (int n : { 8, 16, 32, 48, 64, 128 })
            count.addItem (1000 + n, juce::String (n), true, c.numChannels == n);

        m.addSectionHeader (c.slot);
        m.addSubMenu ("Card", card);
        m.addSubMenu ("Arrives on driver", drv);
        m.addSubMenu ("First channel", first);
        m.addSubMenu ("Channels", count);
        m.addSeparator();
        m.addItem (1, "Rename slot...");

        juce::Component::SafePointer<ConsoleCard> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [safe, i] (int r)
        {
            if (safe == nullptr || r == 0)
                return;

            auto s = safe->link().getSettings();
            if (! juce::isPositiveAndBelow (i, (int) s.slots.size()))
                return;
            auto& slot = s.slots[(size_t) i];

            if (r >= 1000)      slot.numChannels = r - 1000;
            else if (r >= 300)  slot.firstChannel = r - 300;
            else if (r >= 200)  slot.driver = r - 200;
            else if (r >= 100)
            {
                slot.card = r - 100;
                if (slot.card != 0 && slot.numChannels == 0)
                    slot.numChannels = 64;
            }
            else if (r == 1)
            {
                auto* w = new juce::AlertWindow ("Rename slot", {}, juce::MessageBoxIconType::NoIcon);
                w->addTextEditor ("name", slot.slot);
                w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, i, w] (int res)
                {
                    if (safe != nullptr && res == 1)
                    {
                        auto st = safe->link().getSettings();
                        if (juce::isPositiveAndBelow (i, (int) st.slots.size()))
                            st.slots[(size_t) i].slot = w->getTextEditorContents ("name");
                        safe->link().setSettings (st);
                        safe->changed();
                    }
                }), true);
                return;
            }

            safe->link().setSettings (s);
            safe->changed();
        });
    }

    Host& host;
    const int index;
    juce::TextButton settings { "Settings" }, remove { "Remove" },
                     recallPill { "Recall scenes" }, followPill { "Follow selection" };
};

//==============================================================================
// One tile per controller: MIDI devices, the OSC remote, MIDI mappings
class DevicesPanel::ControllerCard : public juce::Component
{
public:
    enum Kind { midiDevice, osc, mappings, addMidi, shortcuts };

    ControllerCard (Host& h, Kind k, juce::String id = {}) : host (h), kind (k), identifier (std::move (id)) {}

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        fillCard (g, area);
        area = area.reduced (14, 10);

        juce::String title, sub;
        juce::Colour light = Theme::textDim.withAlpha (0.4f);

        if (kind == midiDevice)
        {
            for (auto& d : host.midi.getDevices())
                if (d.name == identifier)
                {
                    title = d.name;
                    sub = ! d.connected ? "Not connected: plug it in"
                                        : "MIDI  |  " + juce::String (d.mappings) + (d.mappings == 1 ? " mapping" : " mappings");
                    light = ! d.connected ? Theme::textDim.withAlpha (0.3f) : recent (d.lastActivity, 300) ? Theme::green : Theme::green.withAlpha (0.35f);
                }
        }
        else if (kind == osc)
        {
            title = "OSC remote / Companion";
            sub = host.osc.getStatusText();
            light = ! host.osc.isEnabled() ? Theme::textDim.withAlpha (0.3f)
                  : ! host.osc.isListening() ? Theme::red
                  : recent (host.osc.getLastActivityMs(), 300) ? Theme::green : Theme::green.withAlpha (0.35f);
        }
        else if (kind == mappings)
        {
            const auto n = (int) host.midi.getMappings().size();
            title = "MIDI mappings";
            sub = host.midi.isLearning() ? "Learning... move a control" : juce::String (n) + (n == 1 ? " mapping: click to review" : " mappings: click to review");
            light = host.midi.isLearning() ? Theme::amber : Theme::accent.withAlpha (0.5f);
        }
        else if (kind == shortcuts)
        {
            const int n = host.shortcutCount != nullptr ? host.shortcutCount() : 0;
            title = "Keyboard shortcuts";
            sub = juce::String (n) + (n == 1 ? " shortcut: click to edit" : " shortcuts: click to edit");
            light = Theme::accent.withAlpha (0.5f);
        }
        else
        {
            title = "+ Add MIDI device";
            sub = "Only devices you add are used";
        }

        if (kind != addMidi)
        {
            Theme::drawLight (g, area.removeFromLeft (14).withSizeKeepingCentre (11, 11).toFloat(), light);
            area.removeFromLeft (10);
        }

        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
        g.drawFittedText (title, area.removeFromTop (area.getHeight() / 2), juce::Justification::bottomLeft, 1, 0.8f);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText (sub, area, juce::Justification::topLeft, 2, 0.8f);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (kind == midiDevice)
        {
            juce::PopupMenu m;
            m.addSectionHeader (identifier);
            m.addItem (1, "Remove device (keep its mappings)");
            m.addItem (2, "Remove device and its mappings");
            auto& midi = host.midi;
            const auto name = identifier;
            m.showMenuAsync (juce::PopupMenu::Options(), [&midi, name] (int r)
            {
                if (r == 1) midi.removeDevice (name, true);
                if (r == 2) midi.removeDevice (name, false);
            });
        }
        else if (kind == addMidi)
        {
            const auto available = host.midi.getDevicesToAdd();
            juce::PopupMenu m;
            m.addSectionHeader ("Add a MIDI device");
            for (int i = 0; i < available.size(); ++i)
                m.addItem (i + 1, available[i]);
            if (available.isEmpty())
                m.addItem (-1, "No other MIDI devices found. Plug one in and try again.", false);

            auto& midi = host.midi;
            m.showMenuAsync (juce::PopupMenu::Options(), [&midi, available] (int r)
            {
                if (r > 0 && r <= available.size())
                    midi.addDevice (available[r - 1]);
            });
        }
        else if (kind == shortcuts)
        {
            if (host.openShortcuts != nullptr)
                host.openShortcuts();
        }
        else if (kind == osc)
        {
            auto* w = new juce::AlertWindow ("OSC remote", "LiveRack listens for OSC on this UDP port and sends feedback back to whoever talks to it (for Companion).",
                                             juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("port", juce::String (host.osc.getPort()), "Port");
            w->addComboBox ("on", { "On", "Off" }, "Remote control");
            w->getComboBoxComponent ("on")->setSelectedItemIndex (host.osc.isEnabled() ? 0 : 1);
            w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            auto& o = host.osc;
            w->enterModalState (true, juce::ModalCallbackFunction::create ([&o, w] (int r)
            {
                if (r == 1)
                {
                    o.setEnabled (w->getComboBoxComponent ("on")->getSelectedItemIndex() == 0,
                                  w->getTextEditorContents ("port").getIntValue());
                    o.saveSettings();
                }
            }), true);
        }
        else if (kind == mappings)
        {
            auto& maps = host.midi.getMappings();
            juce::PopupMenu m;

            if (host.midi.isLearning())
                m.addItem (9999, "Cancel MIDI Learn");

            if (maps.empty())
                m.addItem (-1, "No mappings yet. Add a MIDI device, then right-click a User Key > MIDI Learn.", false);

            for (int i = 0; i < (int) maps.size(); ++i)
            {
                juce::PopupMenu item;
                item.addItem (1 + i, "Remove");
                m.addSubMenu (maps[(size_t) i].describeControl() + "   >   " + maps[(size_t) i].describeTarget (host.engine), item);
            }

            if (! maps.empty())
            {
                m.addSeparator();
                m.addItem (10000, "Remove all");
            }

            auto& midi = host.midi;
            m.showMenuAsync (juce::PopupMenu::Options(), [&midi] (int r)
            {
                if (r == 9999)       midi.cancelLearn();
                else if (r == 10000) midi.clearMappings();
                else if (r > 0)      midi.removeMapping (r - 1);
            });
        }
    }

    Host& host;
    const Kind kind;
    const juce::String identifier;
};

//==============================================================================
class DevicesPanel::Content : public juce::Component
{
public:
    explicit Content (Host& h) : host (h)
    {
        addDriver.onClick  = [this] { if (host.engine.addDriver() >= 0) { rebuild(); if (host.openDriverSettings) host.openDriverSettings (host.engine.getNumDrivers() - 1); } };
        addConsole.onClick = [this] { if (host.addConsole) host.addConsole(); };
        addAndMakeVisible (addDriver);
        addAndMakeVisible (addConsole);
        rebuild();
    }

    void rebuild()
    {
        drivers.clear();
        consoles.clear();
        controllers.clear();

        for (int d = 0; d < host.engine.getNumDrivers(); ++d)
            addAndMakeVisible (drivers.add (new DriverCard (host, d)));

        for (int c = 0; c < (int) host.consoles.size(); ++c)
            addAndMakeVisible (consoles.add (new ConsoleCard (host, c)));

        for (auto& d : host.midi.getDevices())
            addAndMakeVisible (controllers.add (new ControllerCard (host, ControllerCard::midiDevice, d.name)));
        addAndMakeVisible (controllers.add (new ControllerCard (host, ControllerCard::addMidi)));
        addAndMakeVisible (controllers.add (new ControllerCard (host, ControllerCard::mappings)));
        addAndMakeVisible (controllers.add (new ControllerCard (host, ControllerCard::shortcuts)));
        addAndMakeVisible (controllers.add (new ControllerCard (host, ControllerCard::osc)));

        addDriver.setEnabled (host.engine.getNumDrivers() < RackEngine::maxDrivers);
        layout();
    }

    void update()
    {
        for (auto* c : consoles) c->updatePills();
        repaint();
        for (auto* c : getChildren()) c->repaint();
    }

    int layout()
    {
        const int w = getWidth();
        int y = 0;
        headings.clear();

        auto section = [&] (const juce::String& title, const juce::String& hint)
        {
            headings.push_back ({ y, title, hint });
            y += 44;
        };

        auto gridOf = [&] (auto& items, juce::Component* addButton, int columns, int h)
        {
            const int cw = (w - (columns - 1) * 14) / columns;
            int i = 0;
            for (auto* c : items)
            {
                c->setBounds ((i % columns) * (cw + 14), y + (i / columns) * (h + 14), cw, h);
                ++i;
            }
            if (addButton != nullptr)
            {
                addButton->setBounds ((i % columns) * (cw + 14), y + (i / columns) * (h + 14), cw, h);
                ++i;
            }
            y += ((i + columns - 1) / columns) * (h + 14) + 16;
        };

        section ("Audio drivers", "Each driver runs on its own audio thread. A rack runs on the driver its channels are patched to.");
        gridOf (drivers, &addDriver, 3, 290);

        section ("Consoles", "One panel per console. Click a card slot to say what's in it and which driver channels it arrives on.");
        gridOf (consoles, &addConsole, 2, 300);

        section ("Controllers", "MIDI devices you add, keyboard shortcuts and the OSC remote. Lights flash as messages arrive.");
        gridOf (controllers, nullptr, 4, 76);

        return y;
    }

    void paint (juce::Graphics& g) override
    {
        for (auto& h : headings)
        {
            g.setColour (Theme::text);
            g.setFont (juce::FontOptions (21.0f, juce::Font::bold));
            g.drawText (h.title, 0, h.y, 260, 34, juce::Justification::centredLeft);
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (h.hint, 240, h.y, getWidth() - 240, 34, juce::Justification::centredLeft, true);
        }

        for (auto* b : { &addDriver, &addConsole })
        {
            if (! b->isVisible()) continue;
            juce::Path outline, dashed;
            outline.addRoundedRectangle (b->getBounds().toFloat().reduced (1.0f), 12.0f);
            const float dashes[] = { 7.0f, 5.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (Theme::border.brighter (0.25f));
            g.fillPath (dashed);
        }
    }

    struct Heading { int y; juce::String title, hint; };
    std::vector<Heading> headings;

    Host& host;
    juce::OwnedArray<DriverCard> drivers;
    juce::OwnedArray<ConsoleCard> consoles;
    juce::OwnedArray<ControllerCard> controllers;

    struct DashedButton : juce::TextButton
    {
        using juce::TextButton::TextButton;
        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            g.setColour (Theme::textDim.brighter (over ? 0.3f : 0.0f));
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
        }
    };
    DashedButton addDriver { "+ Add driver" }, addConsole { "+ Add console" };
};

//==============================================================================
DevicesPanel::DevicesPanel (Host h) : host (std::move (h))
{
    content = std::make_unique<Content> (host);
    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    setSize (1240, 860);
    startTimerHz (5);
}

DevicesPanel::~DevicesPanel() = default;

void DevicesPanel::refresh()
{
    content->rebuild();
    resized();
}

void DevicesPanel::timerCallback()
{
    // Devices plugged in or removed
    if ((int) content->drivers.size() != host.engine.getNumDrivers()
        || (int) content->consoles.size() != (int) host.consoles.size()
        || (int) content->controllers.size() != (int) host.midi.getDevices().size() + 4)
        refresh();

    content->update();
}

void DevicesPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
}

void DevicesPanel::resized()
{
    viewport.setBounds (getLocalBounds().reduced (20, 16));
    const int w = viewport.getWidth() - viewport.getScrollBarThickness() - 4;
    content->setSize (w, 2000);
    content->setSize (w, content->layout());
}
