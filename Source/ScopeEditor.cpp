#include "ScopeEditor.h"
#include "RackStripsView.h"
#include "Theme.h"

namespace
{
    constexpr int colW = 176, gap = 12, headH = 44, itemH = 40, itemGap = 6;

    struct PillDef { const char* label; bool SnapshotScope::* member; };
    const PillDef pillDefs[] = {
        { "Plugin settings", &SnapshotScope::pluginSettings },
        { "Plugin bypass",   &SnapshotScope::pluginBypass },
        { "Rack bypass",     &SnapshotScope::rackBypass },
        { "BPM",             &SnapshotScope::bpm },
    };
}

class ScopeEditor::Map : public juce::Component
{
public:
    Map (RackEngine& e, const int& t) : engine (e), target (t) {}

    void update()
    {
        int tallest = 0;
        for (int r = 0; r < engine.getNumRacks(); ++r)
            tallest = juce::jmax (tallest, engine.getNumSlots (r));

        const int n = engine.getNumRacks();
        setSize (juce::jmax (1, n * (colW + gap)), headH + 10 + tallest * (itemH + itemGap) + 10);
        repaint();
    }

    juce::Rectangle<int> column (int r) const { return { r * (colW + gap), 0, colW, getHeight() }; }
    juce::Rectangle<int> header (int r) const { return column (r).removeFromTop (headH); }
    juce::Rectangle<int> item (int r, int s) const
    {
        return { r * (colW + gap) + 8, headH + 10 + s * (itemH + itemGap), colW - 16, itemH };
    }

    void paint (juce::Graphics& g) override
    {
        for (int r = 0; r < engine.getNumRacks(); ++r)
        {
            const auto cfg = engine.getRackConfig (r);
            const auto colour = RackStripsView::colourFor (engine, r);
            const int n = engine.getNumSlots (r);
            int in = 0;
            for (int s = 0; s < n; ++s)
                if (engine.isSlotInScopeFor (target, r, s)) ++in;

            const bool safe = cfg.recallSafe;
            const float alpha = safe ? 0.5f : 1.0f;

            auto col = column (r).toFloat();
            g.setColour (Theme::panel.withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (col, 10.0f);

            auto head = header (r).toFloat();
            const bool none = in == 0;
            g.setColour ((safe || none ? juce::Colour (0xff3a4048) : colour).withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (head.withHeight (head.getHeight() + 10.0f), 10.0f);
            g.setColour (Theme::panel);
            g.fillRect (head.withTop (head.getBottom()).withHeight (10.0f));

            const auto ink = safe || none ? Theme::text : juce::Colour (0xff101214);
            g.setColour (ink);
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            auto ht = head.reduced (10.0f, 0.0f);
            const juce::String mark = safe ? "SAFE" : in == n ? "ALL" : none ? "NONE" : "PART";
            g.drawText (mark, ht.removeFromRight (44.0f).toNearestInt(), juce::Justification::centredRight);
            g.drawFittedText (cfg.name, ht.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

            for (int s = 0; s < n; ++s)
            {
                auto b = item (r, s).toFloat();
                const bool on = engine.isSlotInScopeFor (target, r, s) && ! safe;
                const bool absent = ! engine.snapshotHasSlot (target, r, s);

                g.setColour (on ? juce::Colour (0xff2f5f94) : Theme::raised);
                g.fillRoundedRectangle (b, 8.0f);
                g.setColour (on ? Theme::accent : Theme::border);
                g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, 1.0f);

                auto box = b.reduced (10.0f, 0.0f).removeFromLeft (18.0f).withSizeKeepingCentre (18.0f, 18.0f);
                if (on)
                {
                    g.setColour (Theme::text);
                    g.fillRoundedRectangle (box, 4.0f);
                    juce::Path tick;
                    tick.startNewSubPath (box.getX() + 4.0f, box.getCentreY());
                    tick.lineTo (box.getX() + 8.0f, box.getBottom() - 4.5f);
                    tick.lineTo (box.getRight() - 3.5f, box.getY() + 4.5f);
                    g.setColour (juce::Colour (0xff1f3a5c));
                    g.strokePath (tick, juce::PathStrokeType (2.2f));
                }
                else
                {
                    g.setColour (Theme::textDim.withAlpha (0.6f));
                    g.drawRoundedRectangle (box, 4.0f, 1.2f);
                }

                auto* p = engine.getPlugin (r, s);
                g.setColour (on ? juce::Colours::white : Theme::textDim);
                g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
                auto label = b.withTrimmedLeft (38.0f).reduced (4.0f, 0.0f).toNearestInt();
                if (absent)
                {
                    g.setFont (juce::FontOptions (10.5f));
                    g.drawText ("not in this snapshot", label.removeFromBottom (14), juce::Justification::centredLeft);
                    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
                }
                g.drawFittedText (p != nullptr ? p->getName() : "?", label, juce::Justification::centredLeft, 1, 0.8f);
            }
        }

        if (engine.getNumRacks() == 0)
        {
            g.setColour (Theme::textDim);
            g.drawText ("No racks yet", getLocalBounds(), juce::Justification::centredLeft);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int r = 0; r < engine.getNumRacks(); ++r)
        {
            if (engine.getRackConfig (r).recallSafe)
                continue;

            if (header (r).contains (e.getPosition()))
            {
                bool all = true;
                for (int s = 0; s < engine.getNumSlots (r); ++s)
                    all = all && (engine.isSlotInScopeFor (target, r, s) || ! engine.snapshotHasSlot (target, r, s));
                for (int s = 0; s < engine.getNumSlots (r); ++s)
                    engine.setSlotInScopeFor (target, r, s, ! all);
                return;
            }

            for (int s = 0; s < engine.getNumSlots (r); ++s)
                if (item (r, s).contains (e.getPosition()))
                {
                    engine.setSlotInScopeFor (target, r, s, ! engine.isSlotInScopeFor (target, r, s));
                    return;
                }
        }
    }

    RackEngine& engine;
    const int& target;
};

//==============================================================================
ScopeEditor::ScopeEditor (RackEngine& e, int snapshot) : engine (e), target (snapshot)
{
    map = std::make_unique<Map> (e, target);

    targetBox.onChange = [this]
    {
        const int id = targetBox.getSelectedId();
        if (id > 0)
        {
            target = id == 1 ? -1 : id - 1000;
            refresh();
        }
    };
    addAndMakeVisible (targetBox);

    for (auto& def : pillDefs)
    {
        auto* b = pills.add (new juce::TextButton (def.label));
        auto member = def.member;
        b->onClick = [this, member]
        {
            auto s = engine.getScopeFor (target);
            s.*member = ! (s.*member);
            engine.setScopeFor (target, s);
        };
        addAndMakeVisible (b);
    }

    summary.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    help.setFont (juce::FontOptions (13.0f));
    help.setColour (juce::Label::textColourId, Theme::textDim);
    help.setText ("Each snapshot has its own scope; new snapshots start with the \"New snapshots\" one. Snapshots never add, remove or move plugins. "
                  "Click a plugin to include or leave it alone, or a rack name for the whole rack. Recall Safe racks are never touched.",
                  juce::dontSendNotification);
    addAndMakeVisible (summary);
    addAndMakeVisible (help);

    viewport.setViewedComponent (map.get(), false);
    viewport.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport);

    setSize (1340, 640);
    refresh();
}

ScopeEditor::~ScopeEditor() = default;

void ScopeEditor::refresh()
{
    // The snapshot list, and which one is being edited
    if (target >= 0 && engine.getSnapshotInfo (target) == nullptr)
        target = -1;

    juce::StringArray names { "New snapshots" };
    for (auto& snap : engine.getSnapshots())
        names.add ("Snapshot " + juce::String (snap.number) + (snap.name.isNotEmpty() ? "  " + snap.name : juce::String()));

    juce::StringArray shown;
    for (int i = 0; i < targetBox.getNumItems(); ++i)
        shown.add (targetBox.getItemText (i));

    if (shown != names)
    {
        targetBox.clear (juce::dontSendNotification);
        targetBox.addItem (names[0], 1);
        int i = 1;
        for (auto& snap : engine.getSnapshots())
            targetBox.addItem (names[i++], 1000 + snap.number);
    }
    targetBox.setSelectedId (target < 0 ? 1 : 1000 + target, juce::dontSendNotification);

    const auto s = engine.getScopeFor (target);
    int i = 0;
    for (auto& def : pillDefs)
        pills[i++]->setToggleState (s.*(def.member), juce::dontSendNotification);

    int total = 0, in = 0, safe = 0;
    for (int r = 0; r < engine.getNumRacks(); ++r)
    {
        if (engine.getRackConfig (r).recallSafe) { ++safe; continue; }
        for (int sl = 0; sl < engine.getNumSlots (r); ++sl)
        {
            ++total;
            if (engine.isSlotInScopeFor (target, r, sl)) ++in;
        }
    }

    summary.setText ("Recalls " + juce::String (in) + " of " + juce::String (total) + " plugins"
                     + (safe > 0 ? "  |  " + juce::String (safe) + (safe == 1 ? " rack" : " racks") + " Recall Safe" : juce::String()),
                     juce::dontSendNotification);
    map->update();
}

void ScopeEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText ("RECALLS", 16, 12, 200, 16, juce::Justification::centredLeft);
    g.drawText ("SCOPE OF", targetBox.getX() - 80, targetBox.getY(), 72, targetBox.getHeight(), juce::Justification::centredRight);
}

void ScopeEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (20);

    auto row = area.removeFromTop (44);
    targetBox.setBounds (row.removeFromRight (300).withSizeKeepingCentre (300, 34));
    row.removeFromRight (90);
    for (auto* p : pills)
    {
        p->setBounds (row.removeFromLeft (juce::jmax (110, p->getBestWidthForHeight (44) + 24)));
        row.removeFromLeft (8);
    }

    area.removeFromTop (14);
    summary.setBounds (area.removeFromTop (22));
    help.setBounds (area.removeFromTop (40));
    area.removeFromTop (8);
    viewport.setBounds (area);
}
