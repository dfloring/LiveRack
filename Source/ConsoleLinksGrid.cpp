#include "ConsoleLinksGrid.h"
#include "RackStripsView.h"
#include "Theme.h"

namespace
{
    constexpr int labelW = 150, colW = 32, rowH = 34, headerH = 110;
}

//==============================================================================
class ConsoleLinksGrid::Grid : public juce::Component
{
public:
    explicit Grid (ConsoleLinksGrid& o) : owner (o) {}

    std::vector<int> racks;

    void rebuild()
    {
        racks.clear();
        if (owner.allRacks)
            for (int r = 0; r < owner.engine.getNumRacks(); ++r)
                racks.push_back (r);
        else
            racks = owner.engine.getVisibleRacks();

        setSize (labelW + juce::jmax (1, (int) racks.size()) * colW + 2, headerH + owner.numRows() * rowH + 2);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto& e = owner.engine;
        paintRows (g);

        for (int ci = 0; ci < (int) racks.size(); ++ci)
        {
            const int r = racks[(size_t) ci];
            const auto cfg = e.getRackConfig (r);
            const auto colour = RackStripsView::colourFor (e, r);
            const int x = labelW + ci * colW;
            const bool isLinked = cfg.consoleChannel > 0 && (cfg.console == owner.console || e.getNumConsoles() < 2);

            // The name runs up the column, in the rack's colour
            g.setColour (colour.withAlpha (isLinked ? 1.0f : 0.5f));
            g.fillRect (x + 2, headerH - 4, colW - 4, 3);
            g.setColour (isLinked ? Theme::text : Theme::textDim);
            g.setFont (juce::FontOptions (Theme::fontSmall, juce::Font::bold));
            const juce::Graphics::ScopedSaveState state (g);
            g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, (float) x, (float) headerH));
            g.drawText (cfg.name + (cfg.stereo ? "  ST" : ""), x + 6, headerH - colW + 2, headerH - 12, colW - 4,
                        juce::Justification::centredLeft, true);
        }
    }

    void paintRows (juce::Graphics& g)
    {
        auto& e = owner.engine;
        const int rows = owner.numRows();
        const auto layout = owner.layout;

        for (int n = 1; n <= rows; ++n)
        {
            const int y = headerH + (n - 1) * rowH;
            const bool sel = n == owner.highlighted;
            const bool offConsole = n > layout.total();

            if (sel)
            {
                g.setColour (Theme::accentDeep);
                g.fillRect (0, y, labelW + (int) racks.size() * colW, rowH);
            }
            if (n == layout.inputs + 1 || n == layout.inputs + layout.mixes + 1)
            {
                g.setColour (Theme::border.brighter (0.2f));
                g.fillRect (0, y, labelW + (int) racks.size() * colW, 2);
            }

            g.setColour (offConsole ? Theme::amber : sel ? juce::Colours::white : Theme::text);
            g.setFont (juce::FontOptions (Theme::fontSmall, juce::Font::bold));
            g.drawText (owner.rowLabel (n), 8, y, labelW - 12, rowH, juce::Justification::centredLeft);

            for (int ci = 0; ci < (int) racks.size(); ++ci)
            {
                const int r = racks[(size_t) ci];
                const auto cell = juce::Rectangle<int> (labelW + ci * colW, y, colW, rowH).reduced (2);
                const auto cfg = e.getRackConfig (r);
                const bool on = owner.linked (r, n);

                g.setColour (on ? RackStripsView::colourFor (e, r) : Theme::panel);
                g.fillRoundedRectangle (cell.toFloat(), 3.0f);

                if (on)
                {
                    const bool left = n == cfg.consoleChannel;
                    g.setColour (juce::Colours::black.withAlpha (0.85f));
                    g.setFont (juce::FontOptions (Theme::fontCaption, juce::Font::bold));
                    g.drawText (cfg.stereo ? (left ? "L" : "R") : "1", cell, juce::Justification::centred);
                }
            }
        }
    }

    void mouseUp (const juce::MouseEvent& ev) override
    {
        if (! ev.mouseWasClicked() || ! owner.engine.isConnected())
            return;

        const int ci = (ev.x - labelW) / colW, n = (ev.y - headerH) / rowH + 1;
        if (ev.x >= labelW && ev.y >= headerH && ci < (int) racks.size() && n >= 1 && n <= owner.numRows())
            owner.toggleLink (racks[(size_t) ci], n);
    }

private:
    ConsoleLinksGrid& owner;
};

//==============================================================================
ConsoleLinksGrid::ConsoleLinksGrid (LiveConnection& e) : engine (e)
{
    grid = std::make_unique<Grid> (*this);

    addAndMakeVisible (allRacksButton);
    allRacksButton.setClickingTogglesState (true);
    allRacksButton.onClick = [this] { allRacks = allRacksButton.getToggleState(); refresh(); };

    help.setColour (juce::Label::textColourId, Theme::textDim);
    help.setFont (juce::FontOptions (Theme::fontBody));
    addAndMakeVisible (help);

    viewport.setViewedComponent (grid.get(), false);
    viewport.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport);

    startTimerHz (4);
    setSize (1300, 720);
    refresh();
}

ConsoleLinksGrid::~ConsoleLinksGrid() { stopTimer(); }

int ConsoleLinksGrid::numRows() const
{
    int rows = layout.total();
    // A rack linked past the end of the list (another console, an old show) stays visible and can be unlinked
    for (int r = 0; r < engine.getNumRacks(); ++r)
    {
        const auto cfg = engine.getRackConfig (r);
        if (cfg.console == console || engine.getNumConsoles() < 2)
            rows = juce::jmax (rows, cfg.consoleChannel, cfg.stereo ? cfg.consoleChannel2 : 0);
    }
    return juce::jmin (rows, 512);   // a wild value from a remote must not make a huge grid
}

juce::String ConsoleLinksGrid::rowLabel (int n) const
{
    if (n > layout.total())             return "CH " + juce::String (n) + " (not on console)";
    if (n <= layout.inputs)             return "CH " + juce::String (n);
    if (n <= layout.inputs + layout.mixes)  return "MIX " + juce::String (n - layout.inputs);
    return "MATRIX " + juce::String (n - layout.inputs - layout.mixes);
}

bool ConsoleLinksGrid::linked (int rack, int channel) const
{
    const auto cfg = engine.getRackConfig (rack);
    if (engine.getNumConsoles() >= 2 && cfg.console != console)
        return false;
    return cfg.consoleChannel == channel || (cfg.stereo && cfg.consoleChannel2 == channel);
}

void ConsoleLinksGrid::toggleLink (int rack, int channel)
{
    if (! juce::isPositiveAndBelow (rack, engine.getNumRacks()) || channel < 1)
        return;

    // One channel links to one rack: any other rack on it lets go
    for (int r = 0; r < engine.getNumRacks(); ++r)
    {
        if (r == rack || ! linked (r, channel))
            continue;
        auto o = engine.getRackConfig (r);
        engine.setRackConsoleLinks (r, o.console, o.consoleChannel == channel ? 0 : o.consoleChannel,
                                                  o.consoleChannel2 == channel ? 0 : o.consoleChannel2);
    }

    const auto cfg = engine.getRackConfig (rack);
    const bool here = cfg.console == console || engine.getNumConsoles() < 2;
    int a = here ? cfg.consoleChannel : 0, b = here && cfg.stereo ? cfg.consoleChannel2 : 0;

    if (a == channel)        a = 0;
    else if (b == channel)   b = 0;
    else if (! cfg.stereo)   a = channel, b = 0;
    else if (a == 0)         a = channel;          // first click L, second R, a third replaces R
    else                     b = channel;

    engine.setRackConsoleLinks (rack, console, a, b);
    refresh();
}

void ConsoleLinksGrid::refresh()
{
    console = juce::jlimit (0, juce::jmax (0, engine.getNumConsoles() - 1), console);

    // The tab buttons are only rebuilt when the number of consoles changes, never from a button's own click
    const int wanted = engine.getNumConsoles() > 1 ? engine.getNumConsoles() : 0;
    if (consoleTabs.size() != wanted)
    {
        consoleTabs.clear();
        for (int i = 0; i < wanted; ++i)
        {
            auto* b = consoleTabs.add (new juce::TextButton);
            b->onClick = [this, i] { console = i; refresh(); };
            addAndMakeVisible (b);
        }
    }
    for (int i = 0; i < wanted; ++i)
    {
        consoleTabs[i]->setButtonText (engine.getConsole (i).displayName);
        consoleTabs[i]->setToggleState (i == console, juce::dontSendNotification);
    }

    layout = engine.getNumConsoles() > 0 ? ConsoleLink::layoutFor (engine.getConsole (console).settings.type)
                                         : ConsoleLink::Layout();
    message = {};
    if (layout.total() == 0)
        message = "No console linked. Set one up in Devices > Console.";
    else if (! engine.isConnected())
        message = "Not connected to the server. Links can't be changed until it is back.";

    const bool noConsole = layout.total() == 0;
    help.setColour (juce::Label::textColourId, message.isNotEmpty() ? Theme::amber : Theme::textDim);
    help.setText (message.isNotEmpty() ? message
                                       : "Click a cell to link a rack to a console channel, click a lit one to unlink. "
                                         "Stereo racks take two channels: first click is L, second is R.",
                  juce::dontSendNotification);

    grid->setVisible (! noConsole);
    allRacksButton.setVisible (! noConsole);
    grid->rebuild();
    resized();
}

void ConsoleLinksGrid::timerCallback()
{
    if (engine.getNumConsoles() == 0)
        return;
    const int sel = engine.getConsole (console).lastSelectedChannel;
    if (sel != highlighted)
    {
        highlighted = sel;
        grid->repaint();
    }
}

void ConsoleLinksGrid::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
}

void ConsoleLinksGrid::resized()
{
    auto area = getLocalBounds().reduced (16);
    auto bar = area.removeFromTop (44);

    for (auto* b : consoleTabs)
    {
        b->setBounds (bar.removeFromLeft (juce::jmin (230, juce::jmax (120, b->getBestWidthForHeight (44) + 20))));
        bar.removeFromLeft (6);
    }
    allRacksButton.setBounds (bar.removeFromRight (110));

    area.removeFromTop (8);
    help.setBounds (area.removeFromTop (22));
    area.removeFromTop (8);
    viewport.setBounds (area);
}
