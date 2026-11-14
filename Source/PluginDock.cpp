#include "PluginDock.h"
#include "RackStripsView.h"
#include "Theme.h"

// A plugin tab that can be dragged
class PluginDock::TabButton : public juce::TextButton
{
public:
    TabButton (const juce::String& buttonText, int r, int s) : juce::TextButton (buttonText), rack (r), slot (s) {}

    void mouseDrag (const juce::MouseEvent& e) override
    {
        juce::TextButton::mouseDrag (e);

        if (dragging || e.getDistanceFromDragStart() < 6)
            return;

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            dragging = true;
            container->startDragging ("slot:" + juce::String (rack) + ":" + juce::String (slot), this,
                                      juce::ScaledImage (createComponentSnapshot (getLocalBounds())), true);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const bool wasDragging = dragging;
        dragging = false;

        if (! wasDragging)
            juce::TextButton::mouseUp (e);
        else
            setState (juce::Button::buttonNormal);
    }

    const int rack, slot;

private:
    bool dragging = false;
};

PluginDock::PluginDock (LiveConnection& c) : engine (c)
{
    for (auto* b : { &rackBypassButton, &bypassButton, &popOutButton, &removeButton })
        addAndMakeVisible (b);

    addAndMakeVisible (prevRackButton);
    addAndMakeVisible (nextRackButton);
    prevRackButton.onClick = [this] { if (onStepRack != nullptr) onStepRack (-1); };
    nextRackButton.onClick = [this] { if (onStepRack != nullptr) onStepRack (1); };

    rackBypassButton.onClick = [this]
    {
        if (rack >= 0)
            engine.setRackBypassed (rack, ! engine.isRackBypassed (rack));
    };
    rackBypassButton.setTooltip ("Bypass the whole rack");

    bypassButton.onClick = [this] { if (onToggleBypass != nullptr) onToggleBypass (rack, slot); };
    popOutButton.onClick = [this] { if (onPopOut != nullptr) onPopOut (rack, slot); };
    removeButton.onClick = [this] { if (onRemove != nullptr) onRemove (rack, slot); };

    addAndMakeVisible (editorHolder);
    show (-1, -1);
}

PluginDock::~PluginDock()
{
    releaseEditor();
}

void PluginDock::releaseEditor()
{
    if (editor != nullptr)
    {
        editor->removeComponentListener (this);
        editorHolder.removeChildComponent (editor.get());
    }

    editor.reset();
    shownProcessor = nullptr;
}

int PluginDock::getPreferredHeight() const
{
    const int chrome = 10 + 32 + 8 + (tabs.isEmpty() ? 0 : 36) + 10;
    return chrome + (editor != nullptr ? juce::jmax (editor->getHeight(), naturalSize.y) : 140);
}

void PluginDock::componentMovedOrResized (juce::Component&, bool, bool wasResized)
{
    if (! wasResized || fitting)
        return;

    // The plugin (or the user) changed its size, unless it's just finishing
    // the size the panel asked for
    if (editor != nullptr && juce::Point<int> (editor->getWidth(), editor->getHeight()) != lastFitted)
        naturalSize = { editor->getWidth(), editor->getHeight() };

    layoutEditor();
    if (onPreferredHeightChanged != nullptr)
        onPreferredHeightChanged();
}

// Fits the editor in the panel: a resizable one is sized to the space (up
// to its own size, within its limits). One that can't resize and doesn't fit
// opens in its own window instead of falling off the edge. (Plugin editors
// are native views on the Mac, so they can't be scaled or scrolled here.)
void PluginDock::layoutEditor()
{
    if (editor == nullptr)
        return;

    const auto avail = editorHolder.getLocalBounds();
    if (avail.getWidth() < 50 || avail.getHeight() < 50)
        return;

    if (editor->isResizable())
    {
        juce::Rectangle<int> b (editor->getX(), editor->getY(),
                                juce::jmin (naturalSize.x, avail.getWidth()),
                                juce::jmin (naturalSize.y, avail.getHeight()));
        if (auto* c = editor->getConstrainer())
            c->checkBounds (b, editor->getBounds(), {}, false, false, true, true);

        if (b.getWidth() != editor->getWidth() || b.getHeight() != editor->getHeight())
        {
            const juce::ScopedValueSetter<bool> svs (fitting, true);
            lastFitted = { b.getWidth(), b.getHeight() };
            editor->setSize (b.getWidth(), b.getHeight());
        }
    }

    editor->setTopLeftPosition (juce::jmax (0, (avail.getWidth() - editor->getWidth()) / 2), 0);

    const bool cutOff = editor->getWidth() > avail.getWidth() + 2 || editor->getHeight() > avail.getHeight() + 2;

    if (cutOff && ! poppedTooBig)
    {
        poppedTooBig = true;
        // Wait for the layout to settle (the panel may be about to grow); if it
        // still doesn't fit, take it out of the panel
        juce::Component::SafePointer<PluginDock> safe (this);
        auto* shown = shownProcessor;
        juce::Timer::callAfterDelay (400, [safe, shown]
        {
            if (safe == nullptr || safe->editor == nullptr || safe->shownProcessor != shown)
                return;
            const auto a = safe->editorHolder.getLocalBounds();
            if (safe->editor->getWidth() > a.getWidth() + 2 || safe->editor->getHeight() > a.getHeight() + 2)
            {
                safe->tooBig = shown;
                safe->releaseEditor();
                safe->show (safe->rack, safe->slot);
            }
        });
    }
}

void PluginDock::show (int newRack, int newSlot)
{
    rack = newRack;
    slot = newSlot;

    auto* plugin = engine.getLocalPlugin (rack, slot);
    const bool hasSlot = engine.getSlot (rack, slot).exists;
    const int heightBefore = getPreferredHeight();

    if (plugin != shownProcessor || ! editorsShown)
        releaseEditor();

    if (plugin != tooBig)
        tooBig = nullptr;

    if (plugin != nullptr && plugin == tooBig)
    {
        message = fullViewAvailable && ! fullView
                    ? "This plugin is too big for this panel. Rack (top bar) shows it in the whole window, or Pop Out opens it in its own window."
                    : "This plugin is too big for this panel. Pop Out opens it in its own window, or make the window bigger.";
    }
    else if (plugin == nullptr && hasSlot)
    {
        // Running on the server
        message = "The plugin's window is on " + engine.getServerName() + ".";
    }
    else if (plugin == nullptr)
    {
        message = rack >= 0 ? "No plugin in this rack yet. Click \"+ Add plugin\" in the strip."
                            : "Add a rack to get started.";
    }
    else if (! editorsShown)
    {
        message.clear();   // not on screen (Overview): no plugin window kept open behind it
    }
    else if (editor == nullptr)
    {
        if (plugin->getActiveEditor() != nullptr)
        {
            message = "This plugin is open in its own window.";
        }
        else
        {
            juce::AudioProcessorEditor* ed = plugin->hasEditor() ? plugin->createEditorIfNeeded() : nullptr;
            if (ed == nullptr)
                ed = new juce::GenericAudioProcessorEditor (*plugin);

            editor.reset (ed);
            shownProcessor = plugin;
            naturalSize = { ed->getWidth(), ed->getHeight() };
            poppedTooBig = false;
            editorHolder.addAndMakeVisible (ed);
            ed->addComponentListener (this);
            message.clear();
        }
    }

    rebuildTabs();
    updateHeader();
    resized();
    repaint();

    if (getPreferredHeight() != heightBefore && onPreferredHeightChanged != nullptr)
        onPreferredHeightChanged();
}

void PluginDock::rebuildTabs()
{
    tabs.clear();

    const auto colour = RackStripsView::colourFor (engine, rack);

    for (int s = 0; s < engine.getNumSlots (rack); ++s)
    {
        const auto plugin = engine.getSlot (rack, s);
        auto* tab = tabs.add (new TabButton (juce::String (s + 1) + ". " + (plugin.exists ? plugin.name : "?"), rack, s));
        tab->setToggleState (s == slot, juce::dontSendNotification);
        tab->setColour (juce::TextButton::buttonOnColourId, Theme::accentDeep);
        juce::ignoreUnused (colour);
        tab->onClick = [this, s] { if (onSelectSlot != nullptr) onSelectSlot (rack, s); };
        addAndMakeVisible (tab);
    }
}

void PluginDock::updateHeader()
{
    const auto plugin = engine.getSlot (rack, slot);
    const bool hasPlugin = plugin.exists;

    const bool bypassed = plugin.bypassed;
    bypassButton.setColour (juce::TextButton::buttonColourId,
                            bypassed ? Theme::amber : getLookAndFeel().findColour (juce::TextButton::buttonColourId));
    bypassButton.setColour (juce::TextButton::textColourOffId, bypassed ? Theme::background : Theme::text);
    bypassButton.setButtonText (bypassed ? "Bypassed" : "Bypass");

    const bool rackBypassed = rack >= 0 && engine.isRackBypassed (rack);
    rackBypassButton.setColour (juce::TextButton::buttonColourId,
                                rackBypassed ? Theme::amber : getLookAndFeel().findColour (juce::TextButton::buttonColourId));
    rackBypassButton.setColour (juce::TextButton::textColourOffId, rackBypassed ? Theme::background : Theme::text);
    rackBypassButton.setButtonText (rackBypassed ? "Rack Bypassed" : "Rack Bypass");
    rackBypassButton.setEnabled (rack >= 0);
    repaint (headerArea);

    for (auto* b : { &bypassButton, &popOutButton, &removeButton })
        b->setEnabled (hasPlugin);
}

int PluginDock::dropIndexAt (juce::Point<int> pos) const
{
    int index = 0;
    for (auto* tab : tabs)
        if (pos.x > tab->getBounds().getCentreX())
            ++index;
    return index;
}

bool PluginDock::isInterestedInDragSource (const SourceDetails& d)
{
    return rack >= 0 && d.description.toString().startsWith ("slot:");
}

void PluginDock::itemDragMove (const SourceDetails& d)
{
    const int i = dropIndexAt (d.localPosition);
    if (i != dropIndex)
    {
        dropIndex = i;
        repaint();
    }
}

void PluginDock::itemDragExit (const SourceDetails&)
{
    dropIndex = -1;
    repaint();
}

void PluginDock::itemDropped (const SourceDetails& d)
{
    const int i = dropIndexAt (d.localPosition);
    dropIndex = -1;
    repaint();

    juce::StringArray parts;
    parts.addTokens (d.description.toString(), ":", "");

    if (parts.size() == 3 && onSlotDropped != nullptr)
        onSlotDropped (parts[1].getIntValue(), parts[2].getIntValue(), rack, i,
                       juce::ModifierKeys::currentModifiers.isAltDown());
}

void PluginDock::paintOverChildren (juce::Graphics& g)
{
    if (dropIndex < 0 || tabs.isEmpty())
        return;

    const auto first = tabs.getFirst()->getBounds();
    int x = first.getX() - 3;

    if (dropIndex >= tabs.size())
        x = tabs.getLast()->getRight() + 1;
    else if (dropIndex > 0)
        x = tabs[dropIndex]->getX() - 3;

    g.setColour (juce::Colours::white);
    g.fillRoundedRectangle ((float) x, (float) first.getY(), 3.0f, (float) first.getHeight(), 1.5f);
}

void PluginDock::updateMeters()
{
    if (rack >= 0 && ! meterArea.isEmpty())
        repaint (meterArea);

    // Each tab shows how long its plugin takes, a few times a second
    if (++meterTicks % 8 == 0 && rack >= 0 && ! engine.isRackBypassed (rack))
    {
        for (int s = 0; s < tabs.size() && s < engine.getNumSlots (rack); ++s)
        {
            const auto plugin = engine.getSlot (rack, s);
            const int pc = (int) std::ceil (engine.getSlotLoad (rack, s) * 100.0f);
            const auto text = juce::String (s + 1) + ". " + (plugin.exists ? plugin.name : "?")
                            + (pc >= 1 ? "   " + juce::String (pc) + "%" : juce::String());
            if (tabs[s]->getButtonText() != text)
                tabs[s]->setButtonText (text);
        }
    }
}

void PluginDock::paint (juce::Graphics& g)
{
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    g.setColour (Theme::border);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.0f);

    // Header: the rack's colour, number and name, channel and latency, meters
    if (rack >= 0)
    {
        const auto cfg = engine.getRackConfig (rack);
        const bool rackBypassed = engine.isRackBypassed (rack);
        auto h = headerArea;

        g.setColour (RackStripsView::colourFor (engine, rack).withAlpha (rackBypassed ? 0.4f : 1.0f));
        g.fillRoundedRectangle (h.removeFromLeft (4).toFloat().reduced (0.0f, 4.0f), 2.0f);
        h.removeFromLeft (10);

        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        const auto num = juce::String (rack + 1);
        g.drawText (num, h.removeFromLeft (20), juce::Justification::centredLeft);

        g.setColour (rackBypassed ? Theme::textDim : Theme::text);
        g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        const int nameW = juce::jmin (240, juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (17.0f, juce::Font::bold), cfg.name) + 4);
        g.drawText (cfg.name, h.removeFromLeft (nameW), juce::Justification::centredLeft, true);
        h.removeFromLeft (14);

        juce::String sub;
        sub << (cfg.consoleChannel > 0 ? "CH " + juce::String (cfg.consoleChannel) : juce::String ("CH -"));
        const double ms = engine.getRackTotalLatencyMs (rack);
        sub << "   " << (ms > 0.005 ? "+" + juce::String (ms, 2) + " ms" : juce::String ("no added latency"));
        if (cfg.latencyGroup > 0)
            sub << "   group " << RackConfig::groupName (cfg.latencyGroup);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (sub, h.removeFromLeft (220), juce::Justification::centredLeft, true);

        // In / out meters, side by side
        auto m = meterArea;
        for (int io = 0; io < 2; ++io)
        {
            auto row = m.removeFromTop (m.getHeight() / (2 - io));
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (io == 0 ? "IN" : "OUT", row.removeFromLeft (28), juce::Justification::centredLeft);
            auto bar = row.withSizeKeepingCentre (row.getWidth(), 6).toFloat();
            const float level = levelFor != nullptr ? juce::jlimit (0.0f, 1.0f, levelFor (rack, io == 1)) : 0.0f;
            const float seg = 3.0f, gap = 1.0f;
            for (float x = bar.getX(); x + seg <= bar.getRight(); x += seg + gap)
            {
                const float n = (x + seg * 0.5f - bar.getX()) / bar.getWidth();
                const auto c = n >= 0.9f ? Theme::red : n >= 0.7f ? Theme::amber : Theme::green;
                g.setColour (n <= level ? c : c.withAlpha (0.08f));
                g.fillRect (x, bar.getY(), seg, bar.getHeight());
            }
        }
    }

    if (message.isNotEmpty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (message, editorHolder.getBounds(), juce::Justification::centred);
    }
}

void PluginDock::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto header = area.removeFromTop (32);
    removeButton.setBounds (header.removeFromRight (84));
    header.removeFromRight (6);
    popOutButton.setBounds (header.removeFromRight (84));
    header.removeFromRight (6);
    bypassButton.setBounds (header.removeFromRight (96));
    header.removeFromRight (6);
    rackBypassButton.setBounds (header.removeFromRight (124));
    header.removeFromRight (16);
    meterArea = header.removeFromRight (juce::jmin (180, juce::jmax (0, header.getWidth() - 360)));
    header.removeFromRight (12);
    prevRackButton.setBounds (header.removeFromLeft (30));
    header.removeFromLeft (2);
    nextRackButton.setBounds (header.removeFromLeft (30));
    header.removeFromLeft (10);
    headerArea = header;

    area.removeFromTop (8);

    if (! tabs.isEmpty())
    {
        auto tabRow = area.removeFromTop (28);
        for (auto* tab : tabs)
        {
            const int w = juce::jmin (264, juce::jmax (110, tab->getBestWidthForHeight (28) + 20 + 44)); // room for the %
            tab->setBounds (tabRow.removeFromLeft (w));
            tabRow.removeFromLeft (4);
        }
        area.removeFromTop (8);
    }

    editorHolder.setBounds (area);
    layoutEditor();

    // A plugin that was too big gets another go when the panel grows enough
    if (tooBig != nullptr && area.getWidth() >= naturalSize.x && area.getHeight() >= naturalSize.y)
    {
        juce::Component::SafePointer<PluginDock> safe (this);
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr && safe->tooBig != nullptr)
            {
                safe->tooBig = nullptr;
                safe->show (safe->rack, safe->slot);
            }
        });
    }
}

void PluginDock::setEditorsShown (bool shouldShow)
{
    if (editorsShown == shouldShow)
        return;

    editorsShown = shouldShow;
    show (rack, slot);
}

void PluginDock::setFullView (bool isFull, bool isAvailable)
{
    if (fullView == isFull && fullViewAvailable == isAvailable)
        return;

    fullView = isFull;
    fullViewAvailable = isAvailable;
    show (rack, slot); // the "too big" message changes, and a plugin may fit now
    resized();
}
