#include "ParameterInspector.h"

ParameterInspector::ParameterInspector (juce::AudioPluginInstance& p, const juce::PluginDescription& loadedFrom,
                                        PluginLibrary& lib, std::function<void()> changed)
    : plugin (p), library (lib), onChanged (std::move (changed)),
      familyKey (PluginLibrary::familyKey (loadedFrom))
{
    sourceLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    hint.setText ("Values update live. Make the plugin compress and watch for a value that moves with it, "
                  "then select that row and press Use for GR.", juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    hint.setFont (juce::FontOptions (12.5f));

    filter.setTextToShowWhenEmpty ("Filter parameters...", juce::Colours::grey);
    filter.onTextChange = [this] { refilter(); };

    useButton.onClick = [this]
    {
        const int row = list.getSelectedRow();
        if (! juce::isPositiveAndBelow (row, visible.size()))
            return;

        library.setGainReductionParameterOverride (familyKey, visible[row]);
        updateSource();
        if (onChanged != nullptr) onChanged();
    };

    clearButton.onClick = [this]
    {
        library.setGainReductionParameterOverride (familyKey, -1);
        updateSource();
        if (onChanged != nullptr) onChanged();
    };

    learnButton.onClick = [this]
    {
        const int row = list.getSelectedRow();
        if (! juce::isPositiveAndBelow (row, visible.size()) || onMidiLearn == nullptr)
            return;

        const int param = visible[row];
        juce::PopupMenu m;
        m.addItem (1, "This plugin only");
        m.addItem (2, "Follow the selected rack (same plugin position)");
        juce::Component::SafePointer<ParameterInspector> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&learnButton), [safe, param] (int r)
        {
            if (safe != nullptr && r > 0 && safe->onMidiLearn != nullptr)
                safe->onMidiLearn (param, r == 2);
        });
    };

    list.setRowHeight (22);

    for (auto* c : std::initializer_list<juce::Component*> { &sourceLabel, &hint, &filter, &useButton, &clearButton, &learnButton, &list })
        addAndMakeVisible (c);

    refilter();
    updateSource();
    startTimerHz (10);
    setSize (720, 600);
}

ParameterInspector::~ParameterInspector() { stopTimer(); }

void ParameterInspector::updateSource()
{
    probe = GainReduction::probe (plugin, library.getGainReductionParameterOverride (familyKey));
    sourceLabel.setText ("GR source: " + probe.describe (plugin), juce::dontSendNotification);
    clearButton.setEnabled (probe.source == GainReduction::Source::userParameter);
    list.repaint();
}

void ParameterInspector::refilter()
{
    visible.clear();
    const auto text = filter.getText().trim().toLowerCase();
    const auto& params = plugin.getParameters();

    for (int i = 0; i < params.size(); ++i)
        if (text.isEmpty() || params[i]->getName (128).toLowerCase().contains (text))
            visible.add (i);

    list.updateContent();
    list.repaint();
}

void ParameterInspector::timerCallback()
{
    list.repaint();

    // Live readout of the current GR source
    float db = 0.0f;
    if (GainReduction::read (plugin, probe, db))
        sourceLabel.setText ("GR source: " + probe.describe (plugin) + "    now: "
                               + (std::isinf (db) || db >= 60.0f ? juce::String ("-inf dB") : db < 0.05f ? juce::String ("0.0 dB") : "-" + juce::String (db, 1) + " dB"),
                             juce::dontSendNotification);
}

void ParameterInspector::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, visible.size()))
        return;

    const int index = visible[row];
    auto* param = plugin.getParameters()[index];
    const bool isGr = probe.parameterIndex == index && probe.source != GainReduction::Source::presonus
                      && probe.source != GainReduction::Source::none;

    if (selected)
        g.fillAll (juce::Colours::steelblue.withAlpha (0.5f));
    else if (isGr)
        g.fillAll (juce::Colour (0xffe8a23a).withAlpha (0.18f));

    juce::String flagText;
    if (! param->isAutomatable()) flagText << "read-only ";
    if (param->isMetaParameter()) flagText << "meta ";
    if (isGr)                     flagText << "GR ";

    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (6, 0);
    g.setFont (juce::FontOptions (13.0f));

    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawText ("#" + juce::String (index), area.removeFromLeft (44), juce::Justification::centredLeft);

    g.setColour (juce::Colours::white);
    g.drawText (param->getName (128), area.removeFromLeft (260), juce::Justification::centredLeft, true);

    g.setColour (juce::Colour (0xffa8d8ff));
    g.drawText (param->getCurrentValueAsText() + " " + param->getLabel(), area.removeFromLeft (180),
                juce::Justification::centredLeft, true);

    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawText (juce::String (param->getValue(), 3), area.removeFromLeft (60), juce::Justification::centredLeft);

    g.setColour (juce::Colour (0xffe8a23a));
    g.drawText (flagText.trim(), area, juce::Justification::centredRight, true);
}

void ParameterInspector::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff23272c));
}

void ParameterInspector::resized()
{
    auto area = getLocalBounds().reduced (12);
    sourceLabel.setBounds (area.removeFromTop (24));
    hint.setBounds (area.removeFromTop (36));
    area.removeFromTop (6);

    auto row = area.removeFromTop (28);
    clearButton.setBounds (row.removeFromRight (80));
    row.removeFromRight (6);
    useButton.setBounds (row.removeFromRight (110));
    row.removeFromRight (6);
    learnButton.setBounds (row.removeFromRight (110));
    row.removeFromRight (10);
    filter.setBounds (row);

    area.removeFromTop (8);
    list.setBounds (area);
}
