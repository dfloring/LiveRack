#include "PluginsPanel.h"
#include "Theme.h"

PluginsPanel::PluginsPanel (LiveConnection& c) : live (c)
{
    for (auto* b : { &scanNew, &rescanAll, &stop, &retryFailed })
        addAndMakeVisible (b);
    addAndMakeVisible (search);
    addAndMakeVisible (list);

    scanNew.setTooltip ("Look for plugins that aren't in the list yet");
    rescanAll.setTooltip ("Scan every plugin again, including those already in the list");
    retryFailed.setTooltip ("Plugins that failed or crashed while scanning are skipped; this lets the next scan try them again");

    scanNew.onClick     = [this] { live.startScan (false); timerCallback(); };
    rescanAll.onClick   = [this] { live.startScan (true); timerCallback(); };
    stop.onClick        = [this] { live.cancelScan(); };
    retryFailed.onClick = [this] { live.clearFailedPlugins(); reload(); };

    search.setTextToShowWhenEmpty ("Search", Theme::textDim);
    search.setJustification (juce::Justification::centredLeft);
    search.setIndents (8, 0);
    search.onTextChange = [this] { filter(); };

    list.setRowHeight (30);
    list.setColour (juce::ListBox::backgroundColourId, Theme::panel);

    live.addChangeListener (this);
    live.refreshKnownPlugins();
    reload();
    startTimerHz (4);
    setSize (820, 620);
}

PluginsPanel::~PluginsPanel()
{
    live.removeChangeListener (this);
}

void PluginsPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    reload();
}

void PluginsPanel::reload()
{
    all = live.getKnownPlugins();
    failed = live.getFailedPlugins();
    filter();
    repaint();
}

void PluginsPanel::filter()
{
    const auto q = search.getText().trim();
    shown.clear();
    for (auto& p : all)
        if (q.isEmpty() || p.name.containsIgnoreCase (q) || p.manufacturer.containsIgnoreCase (q)
            || p.category.containsIgnoreCase (q) || p.format.containsIgnoreCase (q))
            shown.push_back (p);
    list.updateContent();
    list.repaint();
}

// Scan progress; the list again when a scan ends
void PluginsPanel::timerCallback()
{
    scan = live.getScanStatus();
    scanNew.setEnabled (! scan.running && live.isConnected());
    rescanAll.setEnabled (! scan.running && live.isConnected());
    stop.setVisible (scan.running);
    retryFailed.setEnabled (! failed.isEmpty() && ! scan.running);

    if (wasScanning && ! scan.running)
        live.refreshKnownPlugins();   // a server sends the new list; here, read it now
    if (wasScanning && ! scan.running && live.isLocal())
        reload();
    wasScanning = scan.running;
    repaint (getLocalBounds().removeFromTop (110));
}

int PluginsPanel::getNumRows() { return (int) shown.size(); }

void PluginsPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) shown.size()))
        return;

    const auto& p = shown[(size_t) row];
    if (selected)
        g.fillAll (Theme::accentDeep);

    auto r = juce::Rectangle<int> (0, 0, width, height).reduced (10, 0);
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (p.format, r.removeFromRight (70), juce::Justification::centredRight);
    g.drawText (p.category, r.removeFromRight (130), juce::Justification::centredLeft, true);
    g.drawText (p.manufacturer, r.removeFromRight (170), juce::Justification::centredLeft, true);
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
    g.drawText (p.name, r, juce::Justification::centredLeft, true);

    g.setColour (Theme::border);
    g.fillRect (0, height - 1, width, 1);
}

void PluginsPanel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() || ! juce::isPositiveAndBelow (row, (int) shown.size()))
        return;

    const auto p = shown[(size_t) row];
    juce::PopupMenu m;
    m.addSectionHeader (p.name + " (" + p.format + ")");
    m.addItem (1, "Remove from the list (until it's scanned again)");
    juce::Component::SafePointer<PluginsPanel> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, id = p.identifier] (int r)
    {
        if (safe == nullptr || r != 1)
            return;
        safe->live.forgetPlugin (id);
        if (safe->live.isLocal())
            safe->reload();
    });
}

void PluginsPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
    auto area = getLocalBounds().reduced (16);

    auto head = area.removeFromTop (28);
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText ("Plugins", head, juce::Justification::centredLeft);
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (12.5f));
    g.drawText (juce::String ((int) all.size()) + " scanned" + (live.isLocal() ? juce::String() : " on " + live.getServerName())
                    + (failed.isEmpty() ? juce::String() : ", " + juce::String (failed.size()) + " failed"),
                head, juce::Justification::centredRight);

    area.removeFromTop (48);   // the buttons
    auto status = area.removeFromTop (26);
    if (scan.running)
    {
        auto bar = status.removeFromLeft (220).reduced (0, 8).toFloat();
        g.setColour (Theme::raised);
        g.fillRoundedRectangle (bar, 4.0f);
        g.setColour (Theme::accent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, scan.progress)), 4.0f);
        status.removeFromLeft (12);
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText ("Scanning " + juce::File::createFileWithoutCheckingPath (scan.current).getFileName(), status,
                    juce::Justification::centredLeft, true);
    }
    else
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (! live.isConnected() ? "Not connected to " + live.getServerName()
                    : scan.message.isNotEmpty() ? scan.message
                    : juce::String ("Plugins that crash while scanning are skipped and listed as failed."),
                    status, juce::Justification::centredLeft, true);
    }
}

void PluginsPanel::resized()
{
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (36);
    auto row = area.removeFromTop (34);
    scanNew.setBounds (row.removeFromLeft (190));
    row.removeFromLeft (8);
    rescanAll.setBounds (row.removeFromLeft (120));
    row.removeFromLeft (8);
    stop.setBounds (row.removeFromLeft (80));
    retryFailed.setBounds (row.removeFromRight (180));
    row.removeFromRight (8);
    search.setBounds (row.removeFromRight (220));
    area.removeFromTop (6 + 26 + 6);
    list.setBounds (area);
}
