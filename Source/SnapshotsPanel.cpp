#include "SnapshotsPanel.h"
#include "RackStripsView.h"
#include "Theme.h"

namespace
{
    struct PillDef { const char* label; bool SnapshotScope::* member; };
    const PillDef pillDefs[] = {
        { "Plugin settings", &SnapshotScope::pluginSettings },
        { "Plugin bypass",   &SnapshotScope::pluginBypass },
        { "Rack bypass",     &SnapshotScope::rackBypass },
        { "BPM",             &SnapshotScope::bpm },
    };

    juce::String diffKey (int rack, int slot) { return juce::String (rack) + ":" + juce::String (slot); }

    void styleEditor (juce::TextEditor& e)
    {
        e.setColour (juce::TextEditor::backgroundColourId, Theme::well);
        e.setColour (juce::TextEditor::outlineColourId, Theme::border);
        e.setColour (juce::TextEditor::textColourId, Theme::text);
        e.setIndents (8, 6);
    }
}

//==============================================================================
// The snapshots, one row each
class SnapshotsPanel::List : public juce::Component
{
public:
    explicit List (SnapshotsPanel& o) : owner (o) {}

    static constexpr int rowH = 46;

    void update()
    {
        setSize (getWidth(), juce::jmax (rowH, (int) owner.engine.getSnapshots().size() * rowH));
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& list = owner.engine.getSnapshots();
        const int current = owner.host.currentSnapshot != nullptr ? owner.host.currentSnapshot() : -1;

        if (list.empty())
        {
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (13.0f));
            g.drawFittedText ("No snapshots yet. Store one above, or with STORE in the top bar.",
                              getLocalBounds().reduced (14, 0), juce::Justification::centredLeft, 2);
            return;
        }

        int y = 0;
        for (auto& s : list)
        {
            auto row = juce::Rectangle<int> (0, y, getWidth(), rowH);
            y += rowH;
            const bool sel = s.number == owner.selected;

            if (sel)
            {
                g.setColour (Theme::accentDeep);
                g.fillRect (row);
                g.setColour (Theme::accent);
                g.fillRect (row.withWidth (3));
            }
            g.setColour (Theme::border.withAlpha (0.5f));
            g.fillRect (row.withTop (row.getBottom() - 1));

            auto r = row.reduced (14, 0);
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.drawText (juce::String (s.number), r.removeFromLeft (40), juce::Justification::centredLeft);

            // The dot: plugins changed since it was stored
            auto dot = r.removeFromRight (18);
            if (owner.changedSince.count (s.number) > 0 && owner.changedSince[s.number])
            {
                g.setColour (Theme::amber);
                g.fillEllipse (dot.withSizeKeepingCentre (8, 8).toFloat());
            }

            // Triggers: the console scene, MIDI
            juce::StringArray triggers;
            if (owner.host.consoleSceneFor != nullptr)
                if (auto c = owner.host.consoleSceneFor (s.number); c.isNotEmpty())
                    triggers.add (c);
            if (auto m = owner.host.midi.describeTriggerFor (s.number); m.isNotEmpty())
                triggers.add (m);

            auto text = r;
            auto top = text.removeFromTop (text.getHeight() / 2 + (triggers.isEmpty() ? text.getHeight() / 2 : 2));
            g.setColour (Theme::text);
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            const auto name = s.name.isNotEmpty() ? s.name : juce::String ("(no name)");
            const int nameW = juce::jmin (top.getWidth() - 70, juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (14.0f, juce::Font::bold), name) + 4);
            g.drawText (name, top.removeFromLeft (nameW), triggers.isEmpty() ? juce::Justification::centredLeft : juce::Justification::bottomLeft, true);

            if (s.number == current)
            {
                g.setColour (Theme::green);
                g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
                g.drawText ("CURRENT", top.withTrimmedLeft (8), triggers.isEmpty() ? juce::Justification::centredLeft : juce::Justification::bottomLeft);
            }

            if (! triggers.isEmpty())
            {
                g.setColour (Theme::textDim);
                g.setFont (juce::FontOptions (11.5f));
                g.drawText (triggers.joinIntoString (juce::String::fromUTF8 ("  " "\xc2\xb7" "  ")), text, juce::Justification::topLeft, true);
            }
        }
    }

    int numberAt (juce::Point<int> p) const
    {
        const auto& list = owner.engine.getSnapshots();
        const int i = p.y / rowH;
        return juce::isPositiveAndBelow (i, (int) list.size()) ? list[(size_t) i].number : -2;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (const int n = numberAt (e.getPosition()); n != -2)
            owner.select (n);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (const int n = numberAt (e.getPosition()); n != -2 && owner.host.recall != nullptr)
            owner.host.recall (n);
    }

private:
    SnapshotsPanel& owner;
};

//==============================================================================
// What the snapshot recalls: a row per rack, a chip per plugin
class SnapshotsPanel::Scope : public juce::Component
{
public:
    explicit Scope (SnapshotsPanel& o) : owner (o) {}

    static constexpr int nameW = 130, chipH = 30, gap = 6, padY = 9;

    struct Chip { int rack, slot; juce::Rectangle<int> bounds; };
    struct Row { int rack; juce::Rectangle<int> bounds, name; };

    void update()
    {
        layOut();
        repaint();
    }

    void resized() override { layOut(); }

    void layOut()
    {
        chips.clear();
        rows.clear();
        auto& engine = owner.engine;
        const int width = juce::jmax (300, getWidth());
        int y = 0;

        for (int r = 0; r < engine.getNumRacks(); ++r)
        {
            int x = 14 + nameW, lineY = y + padY;
            for (int s = 0; s < engine.getNumSlots (r); ++s)
            {
                auto* p = engine.getPlugin (r, s);
                const auto label = p != nullptr ? p->getName() : juce::String ("?");
                const int w = juce::jlimit (90, 240, juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (12.5f, juce::Font::bold), label) + 40);
                if (x + w > width - 14 && x > 14 + nameW)
                {
                    x = 14 + nameW;
                    lineY += chipH + gap;
                }
                chips.push_back ({ r, s, { x, lineY, w, chipH } });
                x += w + gap;
            }

            const int h = juce::jmax (chipH, lineY - (y + padY) + chipH) + padY * 2;
            rows.push_back ({ r, { 0, y, width, h }, { 14, y, nameW - 8, h } });
            y += h;
        }

        setSize (width, juce::jmax (60, y));
    }

    void paint (juce::Graphics& g) override
    {
        auto& engine = owner.engine;
        const int target = owner.selected;

        if (engine.getNumRacks() == 0)
        {
            g.setColour (Theme::textDim);
            g.drawText ("No racks yet", getLocalBounds().reduced (14, 0), juce::Justification::centredLeft);
            return;
        }

        for (auto& row : rows)
        {
            const auto cfg = engine.getRackConfig (row.rack);
            g.setColour (Theme::border.withAlpha (0.5f));
            g.fillRect (row.bounds.withHeight (1));

            auto name = row.name;
            g.setColour (RackStripsView::colourFor (engine, row.rack).withMultipliedAlpha (cfg.recallSafe ? 0.4f : 1.0f));
            g.fillRoundedRectangle (name.removeFromLeft (4).withSizeKeepingCentre (4, 22).toFloat(), 2.0f);
            name.removeFromLeft (8);

            g.setColour (cfg.recallSafe ? Theme::textDim : Theme::text);
            g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
            auto top = name.removeFromTop (padY + chipH);
            g.drawFittedText (cfg.name, top.withTrimmedTop (padY), juce::Justification::centredLeft, 1, 0.8f);
            if (cfg.recallSafe)
            {
                g.setColour (Theme::amber);
                g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
                g.drawText ("RECALL SAFE", name.removeFromTop (14), juce::Justification::centredLeft);
            }
        }

        for (auto& c : chips)
        {
            const bool safe = engine.getRackConfig (c.rack).recallSafe;
            const bool absent = ! engine.snapshotHasSlot (target, c.rack, c.slot);
            const bool in = ! safe && ! absent && engine.isSlotInScopeFor (target, c.rack, c.slot);
            const bool picked = owner.picking && owner.pickedSlots.count (engine.getSlotId (c.rack, c.slot)) > 0;
            auto b = c.bounds.toFloat();

            if (owner.picking)
            {
                g.setColour (picked ? Theme::amber.withAlpha (0.25f) : Theme::raised);
                g.fillRoundedRectangle (b, chipH / 2.0f);
                g.setColour (picked ? Theme::amber : Theme::border);
                g.drawRoundedRectangle (b.reduced (0.5f), chipH / 2.0f, picked ? 1.6f : 1.0f);
            }
            else if (in)
            {
                g.setColour (Theme::accentDeep);
                g.fillRoundedRectangle (b, chipH / 2.0f);
                g.setColour (Theme::accent.withAlpha (0.7f));
                g.drawRoundedRectangle (b.reduced (0.5f), chipH / 2.0f, 1.0f);
            }
            else
            {
                juce::Path outline;
                outline.addRoundedRectangle (b.reduced (0.5f), chipH / 2.0f);
                const float dashes[] = { 4.0f, 3.0f };
                juce::Path dashed;
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
                g.setColour (juce::Colour (0xff4a515a));
                g.fillPath (dashed);
            }

            auto* p = engine.getPlugin (c.rack, c.slot);
            auto label = c.bounds.reduced (12, 0);
            const auto mark = owner.picking ? (picked ? juce::String::fromUTF8 ("\xe2\x9c\x93") : juce::String())
                                            : (in ? juce::String::fromUTF8 ("\xe2\x97\x8f") : juce::String::fromUTF8 ("\xe2\x97\x8b"));
            g.setColour (owner.picking ? (picked ? Theme::amber : Theme::textDim) : (in ? Theme::text : Theme::textDim));
            g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
            if (mark.isNotEmpty())
                g.drawText (mark, label.removeFromLeft (14), juce::Justification::centredLeft);
            g.drawFittedText ((p != nullptr ? p->getName() : juce::String ("?")) + (absent ? "  (not stored)" : juce::String()),
                              label, juce::Justification::centredLeft, 1, 0.75f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto& engine = owner.engine;
        const int target = owner.selected;
        const auto pos = e.getPosition();

        for (auto& c : chips)
            if (c.bounds.contains (pos))
            {
                if (engine.getRackConfig (c.rack).recallSafe)
                    return;

                if (owner.picking)
                {
                    const int id = engine.getSlotId (c.rack, c.slot);
                    if (! owner.pickedSlots.erase (id))
                        owner.pickedSlots.insert (id);
                    owner.resized(); // the bar's count
                    owner.repaint();
                    repaint();
                    return;
                }

                engine.setSlotInScopeFor (target, c.rack, c.slot, ! engine.isSlotInScopeFor (target, c.rack, c.slot));
                return;
            }

        // A rack's name: the whole rack
        for (auto& row : rows)
            if (row.name.contains (pos))
            {
                if (engine.getRackConfig (row.rack).recallSafe)
                    return;

                if (owner.picking)
                {
                    const int rackId = engine.getRackConfig (row.rack).id;
                    const bool all = owner.pickedRacks.count (rackId) > 0;
                    if (all) owner.pickedRacks.erase (rackId); else owner.pickedRacks.insert (rackId);
                    for (int s = 0; s < engine.getNumSlots (row.rack); ++s)
                    {
                        const int id = engine.getSlotId (row.rack, s);
                        if (all) owner.pickedSlots.erase (id); else owner.pickedSlots.insert (id);
                    }
                    owner.resized();
                    owner.repaint();
                    repaint();
                    return;
                }

                bool all = true;
                for (int s = 0; s < engine.getNumSlots (row.rack); ++s)
                    all = all && (engine.isSlotInScopeFor (target, row.rack, s) || ! engine.snapshotHasSlot (target, row.rack, s));
                for (int s = 0; s < engine.getNumSlots (row.rack); ++s)
                    engine.setSlotInScopeFor (target, row.rack, s, ! all);
                return;
            }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        bool over = false;
        for (auto& c : chips) over = over || c.bounds.contains (e.getPosition());
        for (auto& r : rows) over = over || r.name.contains (e.getPosition());
        setMouseCursor (over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

private:
    SnapshotsPanel& owner;
    std::vector<Chip> chips;
    std::vector<Row> rows;
};

//==============================================================================
// What recalling it would change: a row each, with a tick box
class SnapshotsPanel::Diffs : public juce::Component
{
public:
    explicit Diffs (SnapshotsPanel& o) : owner (o) {}

    static int heightFor (const SnapshotDifference& d) { return 32 + d.details.size() * 17 + 8; }

    void update()
    {
        int h = 0;
        for (auto& d : owner.differences)
            h += heightFor (d);
        setSize (getWidth(), juce::jmax (70, h));
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto& engine = owner.engine;

        if (owner.selected < 0)
        {
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (13.0f));
            g.drawFittedText ("Pick a snapshot to see what recalling it would change.", getLocalBounds().reduced (14, 10),
                              juce::Justification::topLeft, 3);
            return;
        }

        if (owner.differences.empty())
        {
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (13.0f));
            g.drawFittedText ("Nothing: everything it recalls is as it has it.", getLocalBounds().reduced (14, 10),
                              juce::Justification::topLeft, 3);
            return;
        }

        int y = 0;
        for (auto& d : owner.differences)
        {
            auto row = juce::Rectangle<int> (0, y, getWidth(), heightFor (d));
            y += row.getHeight();
            g.setColour (Theme::border.withAlpha (0.5f));
            g.fillRect (row.withHeight (1));

            auto r = row.reduced (14, 8);
            auto line = r.removeFromTop (22);
            const bool ticked = owner.unticked.count (diffKey (d.rack, d.slot)) == 0;

            auto box = line.removeFromLeft (18).withSizeKeepingCentre (16, 16).toFloat();
            line.removeFromLeft (8);
            if (ticked)
            {
                g.setColour (Theme::accent);
                g.fillRoundedRectangle (box, 3.0f);
                juce::Path tick;
                tick.startNewSubPath (box.getX() + 3.5f, box.getCentreY());
                tick.lineTo (box.getX() + 7.0f, box.getBottom() - 4.0f);
                tick.lineTo (box.getRight() - 3.0f, box.getY() + 4.0f);
                g.setColour (Theme::background);
                g.strokePath (tick, juce::PathStrokeType (2.0f));
            }
            else
            {
                g.setColour (Theme::textDim);
                g.drawRoundedRectangle (box.reduced (0.5f), 3.0f, 1.2f);
            }

            const auto count = d.slot < 0 ? juce::String ("bypass")
                             : d.changedSettings < 0 ? juce::String ("settings")
                             : d.changedSettings > 0 ? juce::String (d.changedSettings) + (d.changedSettings == 1 ? " setting" : " settings")
                             : juce::String ("bypass");
            g.setColour (Theme::amber);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (count, line.removeFromRight (80), juce::Justification::centredRight);

            g.setColour (Theme::text);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            const auto rackName = engine.getRackConfig (d.rack).name;
            const int rw = juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (13.0f, juce::Font::bold), rackName) + 8;
            g.drawText (rackName, line.removeFromLeft (rw), juce::Justification::centredLeft, true);
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (13.0f));
            auto* p = d.slot >= 0 ? engine.getPlugin (d.rack, d.slot) : nullptr;
            g.drawText (p != nullptr ? p->getName() : juce::String ("Rack"), line, juce::Justification::centredLeft, true);

            g.setFont (juce::FontOptions (11.5f));
            for (auto& detail : d.details)
                g.drawText (detail, r.removeFromTop (17).withTrimmedLeft (26), juce::Justification::centredLeft, true);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        int y = 0;
        for (auto& d : owner.differences)
        {
            const int h = heightFor (d);
            if (e.y >= y && e.y < y + h)
            {
                const auto k = diffKey (d.rack, d.slot);
                if (! owner.unticked.erase (k))
                    owner.unticked.insert (k);
                owner.updateHeader();
                repaint();
                return;
            }
            y += h;
        }
    }

private:
    SnapshotsPanel& owner;
};

//==============================================================================
SnapshotsPanel::SnapshotsPanel (Host h) : host (std::move (h)), engine (host.engine)
{
    list = std::make_unique<List> (*this);
    scope = std::make_unique<Scope> (*this);
    diffs = std::make_unique<Diffs> (*this);

    // Store a new one
    styleEditor (newNumber);
    styleEditor (newName);
    newNumber.setInputRestrictions (4, "0123456789");
    newNumber.setJustification (juce::Justification::centred);
    newName.setTextToShowWhenEmpty ("Name", Theme::textDim);
    newName.onReturnKey = [this] { storeNew(); };
    storeNewButton.onClick = [this] { storeNew(); };
    storeNewButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
    storeNewButton.setColour (juce::TextButton::textColourOffId, Theme::background);
    for (auto* c : { (juce::Component*) &newNumber, (juce::Component*) &newName, (juce::Component*) &storeNewButton })
        addAndMakeVisible (c);

    listView.setViewedComponent (list.get(), false);
    listView.setScrollBarsShown (true, false);
    addAndMakeVisible (listView);

    defaultsButton.setTooltip ("What new snapshots recall, until you change it for one");
    defaultsButton.setClickingTogglesState (false);
    defaultsButton.onClick = [this] { select (-1); };
    addAndMakeVisible (defaultsButton);

    // Program changes
    for (auto* b : { &followPC, &sendPC })
    {
        b->onClick = [this] { applyMidiControls(); };
        addAndMakeVisible (b);
    }
    for (int ch = 0; ch <= 16; ++ch)
        pcChannel.addItem (ch == 0 ? juce::String ("Any channel") : "Channel " + juce::String (ch), ch + 1);
    for (int ch = 1; ch <= 16; ++ch)
        outChannel.addItem ("Channel " + juce::String (ch), ch);
    for (auto* c : { &pcInput, &pcChannel, &outDevice, &outChannel })
    {
        c->onChange = [this] { applyMidiControls(); };
        addAndMakeVisible (c);
    }
    styleEditor (firstSnapshot);
    firstSnapshot.setInputRestrictions (4, "0123456789");
    firstSnapshot.setJustification (juce::Justification::centred);
    firstSnapshot.onReturnKey = [this] { applyMidiControls(); };
    firstSnapshot.onFocusLost = [this] { applyMidiControls(); };
    addAndMakeVisible (firstSnapshot);

    // The snapshot picked
    styleEditor (nameEdit);
    nameEdit.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    auto rename = [this]
    {
        if (hasSelection())
            if (auto* info = engine.getSnapshotInfo (selected); info != nullptr && info->name != nameEdit.getText().trim())
                engine.renameSnapshot (selected, nameEdit.getText());
    };
    nameEdit.onReturnKey = rename;
    nameEdit.onFocusLost = rename;
    addAndMakeVisible (nameEdit);

    recallButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
    recallButton.setColour (juce::TextButton::textColourOffId, Theme::background);
    recallButton.onClick = [this] { if (hasSelection() && host.recall != nullptr) host.recall (selected); };
    storeOverButton.setButtonText (juce::String::fromUTF8 ("Store over  " "\xe2\x96\xbe"));
    storeOverButton.onClick = [this] { showStoreOverMenu(); };
    duplicateButton.onClick = [this]
    {
        if (! hasSelection())
            return;
        const int to = engine.nextFreeSnapshotNumber (selected);
        if (engine.duplicateSnapshot (selected, to))
        {
            select (to);
            if (host.note != nullptr) host.note ("Duplicated as snapshot " + juce::String (to));
        }
    };
    renumberButton.onClick = [this] { renumber(); };
    deleteButton.setColour (juce::TextButton::textColourOffId, Theme::red);
    deleteButton.onClick = [this]
    {
        if (! hasSelection() || host.confirm == nullptr)
            return;
        const int number = selected;
        juce::Component::SafePointer<SnapshotsPanel> safe (this);
        host.confirm ("Delete snapshot " + juce::String (number) + "?", "Its plugin settings are removed from the show.", "Delete",
                      [safe, number]
                      {
                          if (safe == nullptr) return;
                          safe->engine.deleteSnapshot (number);
                          if (safe->host.note != nullptr) safe->host.note ("Deleted snapshot " + juce::String (number));
                      });
    };
    for (auto* b : { &recallButton, &storeOverButton, &duplicateButton, &renumberButton, &deleteButton })
        addAndMakeVisible (b);

    // What it recalls
    for (auto& def : pillDefs)
    {
        auto* b = pills.add (new juce::TextButton (def.label));
        auto member = def.member;
        b->setClickingTogglesState (false);
        b->onClick = [this, member]
        {
            auto s = engine.getScopeFor (selected);
            s.*member = ! (s.*member);
            engine.setScopeFor (selected, s);
        };
        addAndMakeVisible (b);
    }
    scopeView.setViewedComponent (scope.get(), false);
    scopeView.setScrollBarsShown (true, false);
    addAndMakeVisible (scopeView);

    pickStore.setColour (juce::TextButton::buttonColourId, Theme::amber);
    pickStore.setColour (juce::TextButton::textColourOffId, Theme::background);
    pickStore.onClick = [this] { finishPicking (true); };
    pickCancel.onClick = [this] { finishPicking (false); };
    addChildComponent (pickStore);
    addChildComponent (pickCancel);

    // Differs from now
    diffView.setViewedComponent (diffs.get(), false);
    diffView.setScrollBarsShown (true, false);
    addAndMakeVisible (diffView);
    storeTickedButton.onClick = [this] { storeTicked(); };
    addAndMakeVisible (storeTickedButton);

    setSize (1340, 700);

    const int current = host.currentSnapshot != nullptr ? host.currentSnapshot() : -1;
    selected = engine.getSnapshotInfo (current) != nullptr ? current
             : ! engine.getSnapshots().empty() ? engine.getSnapshots().front().number : -1;
    refresh();
    startTimer (1000);
}

SnapshotsPanel::~SnapshotsPanel() = default;

bool SnapshotsPanel::hasSelection() const
{
    return selected >= 0 && engine.getSnapshotInfo (selected) != nullptr;
}

void SnapshotsPanel::select (int number)
{
    if (picking)
        finishPicking (false);
    selected = number;
    unticked.clear();
    refresh();
}

void SnapshotsPanel::refresh()
{
    if (selected >= 0 && engine.getSnapshotInfo (selected) == nullptr)
        selected = engine.getSnapshots().empty() ? -1 : engine.getSnapshots().front().number;

    if (newNumber.getText().isEmpty() || engine.getSnapshotInfo (newNumber.getText().getIntValue()) != nullptr)
    {
        int highest = 0;
        for (auto& s : engine.getSnapshots())
            highest = juce::jmax (highest, s.number);
        newNumber.setText (juce::String (engine.nextFreeSnapshotNumber (highest)), false);
    }

    list->update();
    scope->update();
    updateDifferences();
    updateHeader();
    updateMidiControls();
    repaint();
}

void SnapshotsPanel::timerCallback()
{
    if (! isShowing())
        return;

    // Plugins change under us: keep "differs from now" and the dots fresh
    std::map<int, bool> changed;
    for (auto& s : engine.getSnapshots())
        if (engine.getSnapshotStoredTime (s.number).toMilliseconds() > 0) // older ones would need every plugin's whole state
            changed[s.number] = ! engine.differencesFromNow (s.number).empty();

    const auto before = differences.size();
    updateDifferences();
    if (changed != changedSince || before != differences.size())
    {
        changedSince = changed;
        list->repaint();
    }
    diffs->repaint();
    updateHeader();
}

void SnapshotsPanel::updateDifferences()
{
    differences = hasSelection() ? engine.differencesFromNow (selected) : std::vector<SnapshotDifference>();
    diffs->update();
}

void SnapshotsPanel::updateHeader()
{
    const bool has = hasSelection();
    auto* info = has ? engine.getSnapshotInfo (selected) : nullptr;

    if (! nameEdit.hasKeyboardFocus (true))
        nameEdit.setText (info != nullptr ? info->name : juce::String(), false);
    nameEdit.setEnabled (has);
    for (auto* b : { &recallButton, &storeOverButton, &duplicateButton, &renumberButton, &deleteButton })
        b->setEnabled (has);

    const auto sc = engine.getScopeFor (selected);
    int i = 0;
    for (auto& def : pillDefs)
        pills[i++]->setToggleState (sc.*(def.member), juce::dontSendNotification);

    // Recalls N of M plugins
    int total = 0, in = 0;
    for (int r = 0; r < engine.getNumRacks(); ++r)
    {
        if (engine.getRackConfig (r).recallSafe)
            continue;
        for (int s = 0; s < engine.getNumSlots (r); ++s)
        {
            ++total;
            if (engine.isSlotInScopeFor (selected, r, s) && engine.snapshotHasSlot (selected, r, s)) ++in;
        }
    }

    juce::StringArray parts;
    if (has)
    {
        const auto console = host.consoleSceneFor != nullptr ? host.consoleSceneFor (selected) : juce::String();
        parts.add ("Console: " + (console.isNotEmpty() ? console : juce::String ("none")));
        const auto stored = engine.getSnapshotStoredTime (selected);
        if (stored.toMilliseconds() > 0)
            parts.add ("Stored " + stored.toString (true, true, false, true));
        parts.add ("Recalls " + juce::String (in) + " of " + juce::String (total) + " plugins");
        if (auto m = host.midi.describeTriggerFor (selected); m.isNotEmpty())
            parts.add ("MIDI: " + m);
        parts.add ("OSC: /liverack/scene/recall " + juce::String (selected));
    }
    else
    {
        parts.add ("What new snapshots recall: " + juce::String (in) + " of " + juce::String (total) + " plugins");
    }
    infoLine = parts.joinIntoString ("    ");

    int ticked = 0;
    for (auto& d : differences)
        if (unticked.count (diffKey (d.rack, d.slot)) == 0)
            ++ticked;
    storeTickedButton.setButtonText (differences.empty() ? juce::String ("Store over")
                                     : ticked == (int) differences.size() ? juce::String ("Store over to keep all these changes")
                                     : "Store over with the " + juce::String (ticked) + " ticked");
    storeTickedButton.setEnabled (has && ticked > 0);
    storeTickedButton.setVisible (has);

    repaint (headerPanel);
}

//==============================================================================
// Program changes
void SnapshotsPanel::updateMidiControls()
{
    const auto& link = host.midi.getSnapshotLink();

    pcInput.clear (juce::dontSendNotification);
    pcInput.addItem ("Any MIDI device", 1);
    auto inputs = host.midi.getInputNames();
    for (int i = 0; i < inputs.size(); ++i)
        pcInput.addItem (inputs[i], i + 2);
    const int inIndex = inputs.indexOf (link.input);
    pcInput.setSelectedId (inIndex >= 0 ? inIndex + 2 : 1, juce::dontSendNotification);

    outDevice.clear (juce::dontSendNotification);
    auto outputs = MidiControl::getOutputNames();
    if (link.output.isNotEmpty() && ! outputs.contains (link.output))
        outputs.add (link.output); // kept while it's unplugged
    outDevice.setTextWhenNothingSelected ("Choose an output");
    for (int i = 0; i < outputs.size(); ++i)
        outDevice.addItem (outputs[i], i + 1);
    const int outIndex = outputs.indexOf (link.output);
    if (outIndex >= 0)
        outDevice.setSelectedId (outIndex + 1, juce::dontSendNotification);

    followPC.setToggleState (link.follow, juce::dontSendNotification);
    sendPC.setToggleState (link.send, juce::dontSendNotification);
    pcChannel.setSelectedId (link.channel + 1, juce::dontSendNotification);
    outChannel.setSelectedId (link.outChannel, juce::dontSendNotification);
    if (! firstSnapshot.hasKeyboardFocus (true))
        firstSnapshot.setText (juce::String (link.firstSnapshot), false);
}

void SnapshotsPanel::applyMidiControls()
{
    MidiControl::SnapshotLink link;
    link.follow = followPC.getToggleState();
    link.input = pcInput.getSelectedId() > 1 ? pcInput.getText() : juce::String();
    link.channel = juce::jmax (0, pcChannel.getSelectedId() - 1);
    link.firstSnapshot = juce::jmax (0, firstSnapshot.getText().getIntValue());
    link.send = sendPC.getToggleState();
    link.output = outDevice.getSelectedId() > 0 ? outDevice.getText() : juce::String();
    link.outChannel = juce::jmax (1, outChannel.getSelectedId());
    host.midi.setSnapshotLink (link);
    list->repaint();
    updateHeader();
}

//==============================================================================
// Storing
void SnapshotsPanel::storeNew()
{
    const int number = newNumber.getText().getIntValue();
    const auto name = newName.getText().trim();
    if (host.store == nullptr)
        return;

    juce::Component::SafePointer<SnapshotsPanel> safe (this);
    auto doStore = [safe, number, name]
    {
        if (safe == nullptr) return;
        safe->host.store (number, name, {});
        safe->newName.clear();
        safe->newNumber.clear();
        safe->select (number);
    };

    if (engine.getSnapshotInfo (number) != nullptr && host.confirm != nullptr)
        host.confirm ("Store over snapshot " + juce::String (number) + "?",
                      "It already exists. Its settings are replaced with the plugins as they are now; what it recalls stays the same.",
                      "Store over", doStore);
    else
        doStore();
}

void SnapshotsPanel::showStoreOverMenu()
{
    if (! hasSelection())
        return;

    int ticked = 0;
    for (auto& d : differences)
        if (unticked.count (diffKey (d.rack, d.slot)) == 0)
            ++ticked;

    juce::PopupMenu m;
    m.addSectionHeader ("Store over snapshot " + juce::String (selected));
    m.addItem (1, "Everything it recalls");
    m.addItem (2, "Only the ticked changes (" + juce::String (ticked) + " of " + juce::String ((int) differences.size()) + ")",
               ticked > 0);
    m.addItem (3, juce::String::fromUTF8 ("Pick racks or plugins" "\xe2\x80\xa6"));

    juce::Component::SafePointer<SnapshotsPanel> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&storeOverButton), [safe] (int r)
    {
        if (safe == nullptr || r == 0)
            return;
        const int number = safe->selected;

        if (r == 1 && safe->host.confirm != nullptr)
            safe->host.confirm ("Store over snapshot " + juce::String (number) + "?",
                                "Every plugin it recalls is stored as it is now. What it recalls stays the same.", "Store over",
                                [safe, number] { if (safe != nullptr) safe->host.store (number, {}, {}); });
        if (r == 2) safe->storeTicked();
        if (r == 3) safe->startPicking();
    });
}

void SnapshotsPanel::storeTicked()
{
    if (! hasSelection() || host.store == nullptr)
        return;

    SnapshotStoreFilter filter;
    filter.everything = false;
    for (auto& d : differences)
    {
        if (unticked.count (diffKey (d.rack, d.slot)) > 0)
            continue;
        if (d.slot < 0) filter.rackIds.push_back (engine.getRackConfig (d.rack).id);
        else            filter.slotIds.push_back (engine.getSlotId (d.rack, d.slot));
    }

    if (filter.rackIds.empty() && filter.slotIds.empty())
        return;

    host.store (selected, {}, filter);
    unticked.clear();
    refresh();
}

void SnapshotsPanel::startPicking()
{
    picking = true;
    pickedSlots.clear();
    pickedRacks.clear();
    resized();
    repaint();
    scope->repaint();
}

void SnapshotsPanel::finishPicking (bool store)
{
    if (store && hasSelection() && host.store != nullptr && ! (pickedSlots.empty() && pickedRacks.empty()))
    {
        SnapshotStoreFilter filter;
        filter.everything = false;
        filter.slotIds.assign (pickedSlots.begin(), pickedSlots.end());
        filter.rackIds.assign (pickedRacks.begin(), pickedRacks.end());
        host.store (selected, {}, filter);
    }

    picking = false;
    pickedSlots.clear();
    pickedRacks.clear();
    resized();
    repaint();
    scope->repaint();
}

void SnapshotsPanel::renumber()
{
    if (! hasSelection())
        return;

    auto* w = new juce::AlertWindow ("Renumber snapshot " + juce::String (selected),
                                     "The console scene, program change and OSC number that recall it change with it.",
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("number", juce::String (selected), "New number");
    w->addButton ("Renumber", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    const int from = selected;
    juce::Component::SafePointer<SnapshotsPanel> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, from] (int res)
    {
        if (safe == nullptr || res != 1)
            return;
        const int to = w->getTextEditorContents ("number").getIntValue();
        if (safe->engine.renumberSnapshot (from, to))
            safe->select (to);
        else if (safe->host.note != nullptr)
            safe->host.note ("Snapshot " + juce::String (to) + " already exists: choose another number");
    }), true);
}

//==============================================================================
void SnapshotsPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    auto panel = [&g] (juce::Rectangle<int> r)
    {
        g.setColour (Theme::panel);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (Theme::border);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
    };

    for (auto r : { leftPanel, headerPanel, scopePanel, diffPanel, midiPanel })
        panel (r);

    auto smallHead = [&g] (juce::Rectangle<int> r, const juce::String& text)
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (text, r, juce::Justification::centredLeft);
    };

    // The list's "new" row label
    smallHead (newNumber.getBounds().withX (leftPanel.getX() + 14).withWidth (40), "NEW");

    // Header: the number, and the line under it
    {
        auto h = headerPanel.reduced (16, 12);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
        g.drawText (hasSelection() ? juce::String (selected) : juce::String ("New"), h.removeFromLeft (80).withHeight (40), juce::Justification::centredLeft);
        if (! hasSelection())
        {
            g.setColour (Theme::text);
            g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
            g.drawText ("Snapshots stored from now on", nameEdit.getBounds(), juce::Justification::centredLeft);
        }
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (infoLine, headerPanel.reduced (16, 12).removeFromBottom (18), juce::Justification::centredLeft, true);
    }

    // Panel titles
    {
        auto t = scopePanel.reduced (14, 0).removeFromTop (44);
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText ("What it recalls", t, juce::Justification::centredLeft);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (picking ? "" : "Click a plugin to include it or leave it alone, or a rack's name for the whole rack",
                    t.withTrimmedLeft (120), juce::Justification::centredLeft, true);

        auto d = diffPanel.reduced (14, 0).removeFromTop (44);
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText ("Differs from now", d.removeFromTop (26).withTrimmedTop (8), juce::Justification::centredLeft);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("What recalling it would change", d, juce::Justification::centredLeft);

        auto m = midiPanel.reduced (14, 0).removeFromTop (30);
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("MIDI program changes", m, juce::Justification::centredLeft);

        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Program 1 recalls snapshot", firstSnapshot.getBounds().withX (midiPanel.getX() + 14).withRight (firstSnapshot.getX() - 8),
                    juce::Justification::centredLeft);
    }

    if (picking)
    {
        g.setColour (Theme::amber.withAlpha (0.15f));
        g.fillRoundedRectangle (pickBar.toFloat(), 6.0f);
        g.setColour (Theme::amber);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText ("Click the plugins (or a rack's name) to store over: " + juce::String ((int) pickedSlots.size()) + " picked",
                    pickBar.reduced (10, 0).withTrimmedRight (200), juce::Justification::centredLeft, true);
    }
}

void SnapshotsPanel::resized()
{
    auto area = getLocalBounds().reduced (12, 8);

    // Left column: the list, then MIDI
    auto left = area.removeFromLeft (juce::jlimit (300, 400, area.getWidth() / 3));
    area.removeFromLeft (12);

    midiPanel = left.removeFromBottom (196);
    left.removeFromBottom (12);
    leftPanel = left;

    {
        auto l = leftPanel.reduced (10);
        auto row = l.removeFromTop (34);
        row.removeFromLeft (38);
        newNumber.setBounds (row.removeFromLeft (50));
        row.removeFromLeft (6);
        storeNewButton.setBounds (row.removeFromRight (72));
        row.removeFromRight (6);
        newName.setBounds (row);
        l.removeFromTop (8);
        defaultsButton.setBounds (l.removeFromBottom (32));
        l.removeFromBottom (8);
        listView.setBounds (l);
        list->setSize (l.getWidth() - (listView.isVerticalScrollBarShown() ? listView.getScrollBarThickness() : 0), list->getHeight());
        list->update();
    }

    {
        auto m = midiPanel.reduced (14, 0);
        m.removeFromTop (30);
        followPC.setBounds (m.removeFromTop (24));
        auto r1 = m.removeFromTop (30).reduced (0, 2);
        pcInput.setBounds (r1.removeFromLeft (r1.getWidth() * 55 / 100));
        r1.removeFromLeft (6);
        pcChannel.setBounds (r1);
        auto r2 = m.removeFromTop (30).reduced (0, 2);
        firstSnapshot.setBounds (r2.removeFromRight (56));
        m.removeFromTop (4);
        sendPC.setBounds (m.removeFromTop (24));
        auto r3 = m.removeFromTop (30).reduced (0, 2);
        outDevice.setBounds (r3.removeFromLeft (r3.getWidth() * 55 / 100));
        r3.removeFromLeft (6);
        outChannel.setBounds (r3);
    }

    // Right: header, then what it recalls and what differs
    headerPanel = area.removeFromTop (112);
    area.removeFromTop (12);
    {
        auto h = headerPanel.reduced (16, 12);
        auto top = h.removeFromTop (40);
        top.removeFromLeft (80);
        auto buttons = top.removeFromRight (juce::jmin (top.getWidth() - 200, 470));
        nameEdit.setBounds (top.withTrimmedRight (12).withSizeKeepingCentre (top.getWidth() - 12, 36));
        const int bw = (buttons.getWidth() - 4 * 6) / 5;
        for (auto* b : { &recallButton, &storeOverButton, &duplicateButton, &renumberButton, &deleteButton })
        {
            b->setBounds (buttons.removeFromLeft (bw).withSizeKeepingCentre (bw, 36));
            buttons.removeFromLeft (6);
        }
    }

    diffPanel = area.removeFromRight (juce::jlimit (280, 380, area.getWidth() * 2 / 5));
    area.removeFromRight (12);
    scopePanel = area;

    {
        auto s = scopePanel.reduced (12, 0);
        s.removeFromTop (44);
        auto pillRow = s.removeFromTop (32);
        for (auto* p : pills)
        {
            p->setBounds (pillRow.removeFromLeft (juce::jmax (100, p->getBestWidthForHeight (32) + 20)));
            pillRow.removeFromLeft (6);
        }
        s.removeFromTop (8);

        pickStore.setVisible (picking);
        pickCancel.setVisible (picking);
        if (picking)
        {
            pickBar = s.removeFromTop (38);
            auto b = pickBar.reduced (6, 4);
            pickCancel.setBounds (b.removeFromRight (80));
            b.removeFromRight (6);
            pickStore.setBounds (b.removeFromRight (100));
            pickStore.setButtonText ("Store " + juce::String ((int) pickedSlots.size()));
            pickStore.setEnabled (! pickedSlots.empty() || ! pickedRacks.empty());
            s.removeFromTop (6);
        }

        scopeView.setBounds (s.withTrimmedBottom (8));
        scope->setSize (scopeView.getWidth() - scopeView.getScrollBarThickness(), scope->getHeight());
        scope->update();
    }

    {
        auto d = diffPanel;
        d.removeFromTop (48);
        storeTickedButton.setBounds (d.removeFromBottom (48).reduced (12, 8));
        diffView.setBounds (d);
        diffs->setSize (diffView.getWidth() - diffView.getScrollBarThickness(), diffs->getHeight());
        diffs->update();
    }
}
