#include "PluginDock.h"
#include "RackStripsView.h"

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

PluginDock::PluginDock (RackEngine& e) : engine (e)
{
    title.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    addAndMakeVisible (title);

    for (auto* b : { &bypassButton, &popOutButton, &removeButton })
        addAndMakeVisible (b);

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
    const int chrome = 10 + 30 + 8 + (tabs.isEmpty() ? 0 : 36) + 10;
    return chrome + (editor != nullptr ? editor->getHeight() : 140);
}

void PluginDock::componentMovedOrResized (juce::Component&, bool, bool wasResized)
{
    if (! wasResized)
        return;

    layoutEditor();
    if (onPreferredHeightChanged != nullptr)
        onPreferredHeightChanged();
}

void PluginDock::layoutEditor()
{
    if (editor != nullptr)
        editor->setTopLeftPosition (juce::jmax (0, (editorHolder.getWidth() - editor->getWidth()) / 2), 0);
}

void PluginDock::show (int newRack, int newSlot)
{
    rack = newRack;
    slot = newSlot;

    auto* plugin = engine.getPlugin (rack, slot);
    const int heightBefore = getPreferredHeight();

    if (plugin != shownProcessor)
        releaseEditor();

    if (plugin == nullptr)
    {
        message = rack >= 0 ? "No plugin in this rack yet. Click \"+ Add plugin\" in the strip."
                            : "Add a rack to get started.";
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
        auto* plugin = engine.getPlugin (rack, s);
        auto* tab = tabs.add (new TabButton (juce::String (s + 1) + ". " + (plugin != nullptr ? plugin->getName() : "?"), rack, s));
        tab->setToggleState (s == slot, juce::dontSendNotification);
        tab->setColour (juce::TextButton::buttonOnColourId, colour.withAlpha (0.6f));
        tab->onClick = [this, s] { if (onSelectSlot != nullptr) onSelectSlot (rack, s); };
        addAndMakeVisible (tab);
    }
}

void PluginDock::updateHeader()
{
    auto* plugin = engine.getPlugin (rack, slot);
    const bool hasPlugin = plugin != nullptr;

    juce::String t;
    if (rack >= 0)
        t << engine.getRackConfig (rack).name;
    if (hasPlugin)
        t << "   " << juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xba")) << "   " << plugin->getName();

    title.setText (t, juce::dontSendNotification);
    title.setColour (juce::Label::textColourId, rack >= 0 ? RackStripsView::colourFor (engine, rack).brighter (0.3f)
                                                         : juce::Colours::white);

    const bool bypassed = engine.isSlotBypassed (rack, slot);
    bypassButton.setColour (juce::TextButton::buttonColourId,
                            bypassed ? juce::Colour (0xffe08a2c) : getLookAndFeel().findColour (juce::TextButton::buttonColourId));
    bypassButton.setButtonText (bypassed ? "Bypassed" : "Bypass");

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

void PluginDock::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff1f2328));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

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

    auto header = area.removeFromTop (30);
    removeButton.setBounds (header.removeFromRight (90));
    header.removeFromRight (6);
    popOutButton.setBounds (header.removeFromRight (90));
    header.removeFromRight (6);
    bypassButton.setBounds (header.removeFromRight (90));
    header.removeFromRight (10);
    title.setBounds (header);

    area.removeFromTop (8);

    if (! tabs.isEmpty())
    {
        auto tabRow = area.removeFromTop (28);
        for (auto* tab : tabs)
        {
            const int w = juce::jmin (220, juce::jmax (110, tab->getBestWidthForHeight (28) + 20));
            tab->setBounds (tabRow.removeFromLeft (w));
            tabRow.removeFromLeft (4);
        }
        area.removeFromTop (8);
    }

    editorHolder.setBounds (area);
    layoutEditor();
}
