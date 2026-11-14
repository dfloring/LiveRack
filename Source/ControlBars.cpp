#include "ControlBars.h"
#include "RackStripsView.h"
#include "Theme.h"
#include "Shortcuts.h"

namespace
{
    void drawHeading (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title, const juce::String& hint)
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (title, area.removeFromLeft (110), juce::Justification::centredLeft);
        g.setColour (Theme::textDim.withAlpha (0.75f));
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (hint, area, juce::Justification::centredLeft, true);
    }
}

//==============================================================================
HotPluginsBar::HotPluginsBar (LiveConnection& e, ActionHost& h) : engine (e), host (h) {}

juce::Rectangle<int> HotPluginsBar::tileBounds (int index) const
{
    auto area = getLocalBounds().reduced (0, 2).withTrimmedTop (18);
    const int gap = 6;
    const int w = (area.getWidth() - gap * (RackEngine::maxHotPlugins - 1)) / RackEngine::maxHotPlugins;
    return { area.getX() + index * (w + gap), area.getY(), w, area.getHeight() };
}

int HotPluginsBar::tileAt (juce::Point<int> p) const
{
    for (int i = 0; i < RackEngine::maxHotPlugins; ++i)
        if (tileBounds (i).contains (p))
            return i;
    return -1;
}

void HotPluginsBar::paint (juce::Graphics& g)
{
    drawHeading (g, getLocalBounds().removeFromTop (16), "HOT PLUGINS",
                 engine.getHotPlugins().empty() ? "Right-click a plugin on a strip > Pin to Hot Plugins" : "");

    const auto hot = engine.getHotPlugins();
    const int selRack = host.getSelectedRack(), selSlot = host.getSelectedSlot();

    for (int i = 0; i < RackEngine::maxHotPlugins; ++i)
    {
        auto r = tileBounds (i).toFloat();
        int rack = -1, slot = -1;
        const bool used = i < (int) hot.size() && engine.findSlot (hot[(size_t) i], rack, slot);

        if (! used)
        {
            juce::Path outline, dashed;
            outline.addRoundedRectangle (r.reduced (0.5f), 6.0f);
            const float dashes[] = { 4.0f, 4.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (Theme::border.brighter (0.15f));
            g.fillPath (dashed);
            continue;
        }

        const bool current = rack == selRack && slot == selSlot;
        const bool target = dragFrom >= 0 && i == dragOver && dragOver != dragFrom;

        g.setColour (current ? Theme::accentDeep : Theme::raised);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (current || target ? Theme::accent : Theme::border);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, target ? 2.0f : 1.0f);

        auto inner = r.reduced (8.0f, 6.0f);
        g.setColour (RackStripsView::colourFor (engine, rack));
        g.fillRoundedRectangle (inner.removeFromLeft (5.0f), 2.0f);
        inner.removeFromLeft (7.0f);

        const auto plugin = engine.getSlot (rack, slot);
        const bool byp = plugin.bypassed || engine.isRackBypassed (rack);

        g.setColour (Theme::text.withAlpha (byp ? 0.5f : 1.0f));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawFittedText (plugin.exists ? plugin.name : "?", inner.removeFromTop (inner.getHeight() * 0.55f).toNearestInt(),
                          juce::Justification::bottomLeft, 1, 0.8f);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ((byp ? "BYP  " : "") + engine.getRackConfig (rack).name, inner.toNearestInt(),
                    juce::Justification::topLeft, true);
    }
}

void HotPluginsBar::mouseDown (const juce::MouseEvent& e)
{
    const int i = tileAt (e.getPosition());
    const auto hot = engine.getHotPlugins();
    if (i < 0 || i >= (int) hot.size())
        return;

    const int id = hot[(size_t) i];

    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addItem (1, "Unpin");
        m.addItem (2, "Move left", i > 0);
        m.addItem (3, "Move right", i < (int) hot.size() - 1);

        m.showMenuAsync (juce::PopupMenu::Options(), [this, i, id] (int result)
        {
            auto list = engine.getHotPlugins();
            if (result == 1) { list.erase (std::remove (list.begin(), list.end(), id), list.end()); engine.setHotPlugins (list); }
            if (result == 2 && i > 0)                       { std::swap (list[(size_t) i], list[(size_t) i - 1]); engine.setHotPlugins (list); }
            if (result == 3 && i + 1 < (int) list.size())   { std::swap (list[(size_t) i], list[(size_t) i + 1]); engine.setHotPlugins (list); }
        });
        return;
    }

    dragFrom = i;
    dragOver = i;
}

void HotPluginsBar::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom < 0)
        return;

    const int over = tileAt (e.getPosition());
    if (over != dragOver)
    {
        dragOver = over;
        repaint();
    }
}

void HotPluginsBar::mouseUp (const juce::MouseEvent& e)
{
    const auto hot = engine.getHotPlugins();
    const int from = dragFrom, to = juce::jmin (dragOver, (int) hot.size() - 1);
    dragFrom = dragOver = -1;

    if (from < 0 || from >= (int) hot.size())
        return;

    if (to >= 0 && to != from && e.getDistanceFromDragStart() > 6)
    {
        auto list = hot;
        const int id = list[(size_t) from];
        list.erase (list.begin() + from);
        list.insert (list.begin() + to, id);
        engine.setHotPlugins (list);
        return;
    }

    if (e.getDistanceFromDragStart() <= 6 && onOpen != nullptr)
        onOpen (hot[(size_t) from]);

    repaint();
}

//==============================================================================
UserKeysBar::UserKeysBar (RackEngine& e, ActionHost& h) : engine (e), host (h)
{
    load();
}

juce::Rectangle<int> UserKeysBar::keyBounds (int index) const
{
    auto area = getLocalBounds().reduced (0, 2).withTrimmedTop (18);
    const int gap = 8;
    const int w = (area.getWidth() - gap * (numKeys - 1)) / numKeys;
    return { area.getX() + index * (w + gap), area.getY(), w, area.getHeight() };
}

void UserKeysBar::setKey (int i, const Action& a)
{
    if (! juce::isPositiveAndBelow (i, numKeys))
        return;

    keys[(size_t) i] = a;
    save();
    repaint();
    if (onChanged != nullptr)
        onChanged();
}

void UserKeysBar::setShortcut (int i, const juce::KeyPress& k)
{
    if (! juce::isPositiveAndBelow (i, numKeys))
        return;
    shortcuts[(size_t) i] = k;
    save();
    repaint();
}

int UserKeysBar::keyForShortcut (const juce::KeyPress& k) const
{
    for (int i = 0; i < numKeys; ++i)
        if (shortcuts[(size_t) i].isValid() && shortcuts[(size_t) i] == k)
            return i;
    return -1;
}

void UserKeysBar::press (int i)
{
    if (juce::isPositiveAndBelow (i, numKeys) && keys[(size_t) i].type != Action::none)
        host.performAction (keys[(size_t) i], -1);
}

void UserKeysBar::paint (juce::Graphics& g)
{
    drawHeading (g, getLocalBounds().removeFromTop (16), "USER KEYS", "");

    for (int i = 0; i < numKeys; ++i)
    {
        const auto& a = keys[(size_t) i];
        const auto r = keyBounds (i).toFloat();
        const bool assigned = a.type != Action::none;
        const bool lit = assigned && host.isActionActive (a);
        const bool down = i == pressedKey;

        auto fill = lit ? Theme::amber : assigned ? Theme::raised : Theme::panel;
        if (down) fill = fill.brighter (0.15f);

        g.setColour (fill);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (lit ? Theme::amber : Theme::border);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        auto inner = r.reduced (8.0f, 5.0f);
        const auto ink = lit ? juce::Colour (0xff101214) : Theme::text;

        g.setColour (ink.withAlpha (0.55f));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        const auto& sc = shortcuts[(size_t) i];
        g.drawText (sc.isValid() ? Shortcuts::describe (sc) : juce::String(), inner.removeFromTop (12.0f).toNearestInt(), juce::Justification::topRight);

        g.setColour (assigned ? ink : Theme::textDim);
        g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        g.drawFittedText (assigned ? a.describe (engine) : "Empty", inner.removeFromTop (inner.getHeight() * 0.55f).toNearestInt(),
                          juce::Justification::centred, 1, 0.75f);

        g.setColour (ink.withAlpha (0.7f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (a.category(), inner.toNearestInt(), juce::Justification::centredTop, true);
    }
}

void UserKeysBar::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < numKeys; ++i)
    {
        if (! keyBounds (i).contains (e.getPosition()))
            continue;

        if (e.mods.isPopupMenu() || keys[(size_t) i].type == Action::none)
        {
            juce::PopupMenu m;
            m.addItem (1, "Assign...");
            m.addItem (4, "Set keyboard shortcut... (now " + Shortcuts::describe (shortcuts[(size_t) i]) + ")");
            m.addItem (2, "MIDI Learn", keys[(size_t) i].type != Action::none);
            m.addItem (3, "Clear", keys[(size_t) i].type != Action::none);

            juce::Component::SafePointer<UserKeysBar> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options(), [safe, i] (int result)
            {
                if (safe == nullptr) return;

                if (result == 1)
                    Action::showChooser (safe->engine, safe->keys[(size_t) i], safe->host.getSelectedRack(),
                                         [safe, i] (Action a) { if (safe != nullptr) safe->setKey (i, a); });
                if (result == 2 && safe->onMidiLearn != nullptr)
                    safe->onMidiLearn (i);
                if (result == 3)
                    safe->setKey (i, {});
                if (result == 4 && safe->onSetShortcut != nullptr)
                    safe->onSetShortcut (i);
            });
            return;
        }

        pressedKey = i;
        repaint();
        press (i);

        juce::Component::SafePointer<UserKeysBar> safe (this);
        juce::Timer::callAfterDelay (120, [safe] { if (safe != nullptr) { safe->pressedKey = -1; safe->repaint(); } });
        return;
    }
}

// Kept in the show (so a show brings its own keys) and in user-keys.xml
// (the keys a new show starts with)
juce::XmlElement UserKeysBar::toXml() const
{
    juce::XmlElement xml ("USERKEYS");
    for (int i = 0; i < numKeys; ++i)
    {
        auto* e = xml.createNewChildElement ("KEY");
        keys[(size_t) i].writeTo (*e);
        e->setAttribute ("shortcut", Shortcuts::toText (shortcuts[(size_t) i]));
    }
    return xml;
}

void UserKeysBar::save() const
{
    const auto xml = toXml();
    xml.writeTo (RackEngine::getSettingsDir().getChildFile ("user-keys.xml"));
    engine.setShowExtra (xml);
}

void UserKeysBar::load()
{
    for (int i = 0; i < numKeys; ++i)
    {
        shortcuts[(size_t) i] = juce::KeyPress (juce::KeyPress::F1Key + i); // defaults: F1-F8
        keys[(size_t) i] = {};
    }

    std::unique_ptr<juce::XmlElement> fromFile;
    const juce::XmlElement* xml = engine.getShowExtra ("USERKEYS");
    if (xml == nullptr)
    {
        fromFile = juce::parseXML (RackEngine::getSettingsDir().getChildFile ("user-keys.xml"));
        xml = fromFile.get();
    }

    if (xml != nullptr)
    {
        int i = 0;
        for (auto* k : xml->getChildWithTagNameIterator ("KEY"))
            if (i < numKeys)
            {
                if (k->hasAttribute ("shortcut"))
                    shortcuts[(size_t) i] = Shortcuts::fromText (k->getStringAttribute ("shortcut"));
                keys[(size_t) i++] = Action::readFrom (*k);
            }
        engine.setShowExtra (toXml());
        repaint();
        return;
    }

    // First run: some useful defaults
    keys[0].type = Action::rackBypass;    keys[0].rack = -1;
    keys[1].type = Action::pluginBypass;  keys[1].slotId = 0;
    keys[2].type = Action::prevScene;
    keys[3].type = Action::nextScene;
    keys[4].type = Action::hotPlugin;     keys[4].number = 1;
    keys[5].type = Action::hotPlugin;     keys[5].number = 2;
    keys[6].type = Action::twoWindows;
    keys[7].type = Action::tapTempo;
    engine.setShowExtra (toXml());
}

//==============================================================================
PageTabs::PageTabs (LiveConnection& e) : engine (e) {}

std::vector<juce::Rectangle<int>> PageTabs::layoutTabs() const
{
    std::vector<juce::Rectangle<int>> tabs;
    auto area = getLocalBounds().reduced (0, 3);
    const juce::FontOptions font (14.0f, juce::Font::bold);

    auto add = [&] (const juce::String& text)
    {
        const int w = juce::jlimit (70, 220, juce::GlyphArrangement::getStringWidthInt (font, text) + 44);
        tabs.push_back (area.removeFromLeft (w));
        area.removeFromLeft (6);
    };

    add ("All");
    for (auto& p : engine.getPages())
        add (p.name);
    add ("+ Page");
    return tabs;
}

void PageTabs::paint (juce::Graphics& g)
{
    const auto tabs = layoutTabs();
    const auto pages = engine.getPages();
    const int current = engine.getCurrentPage();

    for (int i = 0; i < (int) tabs.size(); ++i)
    {
        auto r = tabs[(size_t) i].toFloat();
        const bool isAdd = i == (int) tabs.size() - 1;
        const int page = i - 1; // -1 = All
        const bool on = ! isAdd && page == current;

        if (isAdd)
        {
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
            g.drawText ("+ Page", r.toNearestInt(), juce::Justification::centred);
            continue;
        }

        // Tabs: the current one is underlined with the accent
        if (on)
        {
            g.setColour (Theme::accent);
            g.fillRoundedRectangle (r.removeFromBottom (2.0f).reduced (6.0f, 0.0f), 1.0f);
        }

        const juce::String name = page < 0 ? juce::String ("All") : pages[(size_t) page].name;
        int n = engine.getNumRacks();
        if (page >= 0)
        {
            n = 0;
            for (int id : pages[(size_t) page].rackIds)
                if (engine.findRackById (id) >= 0) ++n;
        }

        auto t = r.reduced (12.0f, 0.0f);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (juce::String (n), t.removeFromRight (18.0f).toNearestInt(), juce::Justification::centredRight);
        g.setColour (on ? Theme::text : Theme::textDim);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawFittedText (name, t.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }
}

void PageTabs::mouseDown (const juce::MouseEvent& e)
{
    const auto tabs = layoutTabs();

    for (int i = 0; i < (int) tabs.size(); ++i)
    {
        if (! tabs[(size_t) i].contains (e.getPosition()))
            continue;

        if (i == (int) tabs.size() - 1)
        {
            if (onAddPage != nullptr) onAddPage();
            return;
        }

        const int page = i - 1;
        engine.setCurrentPage (page);

        if (e.mods.isPopupMenu() && page >= 0 && onPageMenu != nullptr)
            onPageMenu (page);
        return;
    }
}
