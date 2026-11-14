#include "RoutingMatrix.h"
#include "RackStripsView.h"
#include "Theme.h"

namespace
{
    constexpr int labelW = 230, colW = 32, rowH = 34, groupH = 24, numberH = 24, perPage = 32;
}

//==============================================================================
class RoutingMatrix::Grid : public juce::Component,
                            public juce::TooltipClient
{
public:
    explicit Grid (RoutingMatrix& o) : owner (o) {}

    struct Row { int rack, side; };
    std::vector<Row> rows;

    void rebuild()
    {
        rows.clear();
        auto& e = owner.engine;
        for (int r = 0; r < e.getNumRacks(); ++r)
        {
            const auto cfg = e.getRackConfig (r);
            for (int c = 0; c < (owner.outputs ? cfg.numOutputs() : cfg.numInputs()); ++c)
                rows.push_back ({ r, c });
        }

        setSize (labelW + perPage * colW + 2, groupH + numberH + (int) rows.size() * rowH + 2);
        repaint();
    }

    int firstChannel() const { return owner.page * perPage; } // 0-based

    // Which rack channels use this driver channel (for clash warnings)
    int usersOf (int channel) const
    {
        int n = 0;
        auto& e = owner.engine;
        for (int r = 0; r < e.getNumRacks(); ++r)
        {
            const auto cfg = e.getRackConfig (r);
            if (cfg.driver != owner.driver)
                continue;
            for (int c = 0; c < (owner.outputs ? cfg.numOutputs() : cfg.numInputs()); ++c)
                if ((owner.outputs ? cfg.output[c] : cfg.input[c]) == channel)
                    ++n;
        }
        return n;
    }

    void paint (juce::Graphics& g) override
    {
        auto& e = owner.engine;
        const int first = firstChannel();
        const auto sel = selectedRow();
        int selColumn = -1;

        if (sel >= 0)
        {
            const auto cfg = e.getRackConfig (rows[(size_t) sel].rack);
            const int ch = owner.outputs ? cfg.output[rows[(size_t) sel].side] : cfg.input[rows[(size_t) sel].side];
            if (cfg.driver == owner.driver && ch >= first && ch < first + perPage)
                selColumn = ch - first;
        }

        // Column groups of 8, labelled with the driver (and console card slot)
        const auto shownDriver = e.getDriver (owner.driver);
        const auto driverName = shownDriver.label;
        for (int b = 0; b < perPage / 8; ++b)
        {
            const int x = labelW + b * 8 * colW;
            const int ch1 = first + b * 8 + 1;
            juce::String label = juce::String (ch1) + "-" + juce::String (ch1 + 7);

            if (auto slot = shownDriver.blockLabels[(ch1 - 1) / 8]; slot.isNotEmpty())
                label = slot + "  " + label;

            g.setColour (Theme::border.brighter (0.2f));
            g.fillRect (x, 2, 2, groupH + numberH + (int) rows.size() * rowH);
            g.setColour (Theme::text.withAlpha (0.85f));
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (label, x + 8, 0, 8 * colW - 10, groupH, juce::Justification::bottomLeft, true);
        }

        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (owner.outputs ? "RACK OUTPUT  >  " + driverName : "RACK INPUT  <  " + driverName,
                    4, 0, labelW - 8, groupH + numberH - 4, juce::Justification::bottomLeft, true);

        // Channel numbers
        for (int i = 0; i < perPage; ++i)
        {
            const int ch = first + i;
            const auto cell = juce::Rectangle<int> (labelW + i * colW, groupH, colW, numberH);
            if (i == selColumn)
            {
                g.setColour (Theme::accentDeep);
                g.fillRect (cell);
            }
            g.setColour (i == selColumn ? juce::Colours::white : usersOf (ch) > 0 ? Theme::text : Theme::textDim.withAlpha (0.7f));
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (juce::String (ch + 1), cell, juce::Justification::centred);
        }

        // Rows
        for (int ri = 0; ri < (int) rows.size(); ++ri)
        {
            const auto& row = rows[(size_t) ri];
            const auto cfg = e.getRackConfig (row.rack);
            const auto colour = RackStripsView::colourFor (e, row.rack);
            const int y = groupH + numberH + ri * rowH;
            const bool isSel = ri == sel;
            const int patched = owner.outputs ? cfg.output[row.side] : cfg.input[row.side];
            const bool onThisDriver = cfg.driver == owner.driver;

            auto label = juce::Rectangle<int> (0, y, labelW, rowH).reduced (0, 1);
            g.setColour (isSel ? Theme::accentDeep : juce::Colours::transparentBlack);
            g.fillRect (label);
            g.setColour (colour);
            g.fillRoundedRectangle (label.removeFromLeft (10).reduced (2, 5).toFloat(), 2.0f);

            g.setColour (Theme::text);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            auto right = label.removeFromRight (78);
            g.drawText (cfg.name, label.reduced (6, 0), juce::Justification::centredLeft, true);

            g.setFont (juce::FontOptions (11.5f));
            g.setColour (Theme::textDim);
            const int sides = owner.outputs ? cfg.numOutputs() : cfg.numInputs();
            const juce::String side = sides == 2 ? (row.side == 0 ? "L" : "R") : "M";
            const juce::String where = patched < 0 ? juce::String ("-")
                                     : (onThisDriver ? "" : "D" + juce::String (cfg.driver + 1) + " ") + juce::String (patched + 1);
            g.drawText (side + "   " + where, right.reduced (6, 0), juce::Justification::centredRight);

            for (int i = 0; i < perPage; ++i)
            {
                const int ch = first + i;
                auto cell = juce::Rectangle<int> (labelW + i * colW, y, colW, rowH);
                const bool band = (i / 8) % 2 == 0;
                const bool cross = isSel || i == selColumn;

                g.setColour (cross ? juce::Colour (0xff1f2d3e) : band ? Theme::panel : juce::Colour (0xff191c20));
                g.fillRect (cell.reduced (0, 1).withTrimmedRight (1));

                const bool on = onThisDriver && patched == ch;
                const auto c = cell.getCentre().toFloat();

                if (on)
                {
                    const bool clash = owner.outputs && usersOf (ch) > 1;
                    if (clash)
                    {
                        g.setColour (Theme::red);
                        g.drawEllipse (juce::Rectangle<float> (20.0f, 20.0f).withCentre (c), 2.0f);
                    }
                    g.setColour (clash ? Theme::red : colour);
                    g.fillEllipse (juce::Rectangle<float> (15.0f, 15.0f).withCentre (c));
                }
                else
                {
                    g.setColour (juce::Colour (0xff343a42));
                    g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (c));
                }
            }
        }

        if (rows.empty())
        {
            g.setColour (Theme::textDim);
            g.drawText ("No racks yet", getLocalBounds(), juce::Justification::centred);
        }
    }

    int selectedRow() const
    {
        for (int i = 0; i < (int) rows.size(); ++i)
            if (rows[(size_t) i].rack == owner.selectedRack && rows[(size_t) i].side == owner.selectedSide)
                return i;
        return -1;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int ri = (e.y - groupH - numberH) / rowH;
        if (e.y < groupH + numberH || ! juce::isPositiveAndBelow (ri, (int) rows.size()))
            return;

        const auto row = rows[(size_t) ri];
        owner.selectedRack = row.rack;
        owner.selectedSide = row.side;

        if (e.x >= labelW)
        {
            const int col = (e.x - labelW) / colW;
            if (! juce::isPositiveAndBelow (col, perPage))
                return;

            const int ch = firstChannel() + col;
            auto cfg = owner.engine.getRackConfig (row.rack);
            int& target = owner.outputs ? cfg.output[row.side] : cfg.input[row.side];
            const bool wasOn = cfg.driver == owner.driver && target == ch;
            owner.note.clear();

            if (cfg.driver != owner.driver)
            {
                // Patching to another driver moves the rack there; its other
                // channels belonged to the old driver
                cfg.driver = owner.driver;
                for (int c = 0; c < 2; ++c)
                {
                    if (owner.outputs) cfg.input[c] = -1;
                    else               cfg.output[c] = -1;
                    if (&target != (owner.outputs ? &cfg.output[c] : &cfg.input[c]))
                        (owner.outputs ? cfg.output[c] : cfg.input[c]) = -1;
                }
            }

            target = wasOn ? -1 : ch;

            // Auto-patch: an unpatched output follows the input to the same
            // number, if nothing else uses it
            if (! owner.outputs && ! wasOn)
                owner.autoPatchOutputs (row.rack, cfg, row.side, ch);

            owner.engine.setRackRouting (row.rack, cfg.driver, cfg.input, cfg.output);
        }

        if (owner.onSelectRack != nullptr)
            owner.onSelectRack (row.rack);

        repaint();
    }

    juce::String getTooltip() override
    {
        const auto p = getMouseXYRelative();
        const int ri = (p.y - groupH - numberH) / rowH;
        if (p.x < labelW || p.y < groupH + numberH || ! juce::isPositiveAndBelow (ri, (int) rows.size()))
            return {};

        const int ch = firstChannel() + (p.x - labelW) / colW;
        const auto shownDriver = owner.engine.getDriver (owner.driver);
        const auto name = (owner.outputs ? shownDriver.outNames : shownDriver.inNames)[ch];
        return owner.engine.getRackConfig (rows[(size_t) ri].rack).name + "  <>  channel " + juce::String (ch + 1)
               + (name.isNotEmpty() ? " (" + name + ")" : juce::String());
    }

    RoutingMatrix& owner;
};

//==============================================================================
bool RoutingMatrix::outputFree (int rack, int driverIndex, int ch) const
{
    const auto status = engine.getDriver (driverIndex);
    if (status.open && ch >= status.numOuts)
        return false;

    for (int r = 0; r < engine.getNumRacks(); ++r)
    {
        if (r == rack) continue;
        const auto c = engine.getRackConfig (r);
        if (c.driver != driverIndex) continue;
        for (int i = 0; i < c.numOutputs(); ++i)
            if (c.output[i] == ch)
                return false;
    }
    return true;
}

void RoutingMatrix::autoPatchOutputs (int rack, RackConfig& cfg, int side, int ch)
{
    auto tryPatch = [&] (int outSide, int outCh)
    {
        if (cfg.output[outSide] >= 0)
            return; // patched by hand: leave it
        if (outputFree (rack, cfg.driver, outCh))
            cfg.output[outSide] = outCh;
        else
            note = "Output " + juce::String (outCh + 1) + " is already used by another rack, so " + cfg.name
                   + "'s output was left unpatched.";
    };

    if (cfg.isMonoToStereo())
    {
        tryPatch (0, ch);
        tryPatch (1, ch + 1);
    }
    else
    {
        tryPatch (side, ch);
    }
}

RoutingMatrix::RoutingMatrix (LiveConnection& e) : engine (e)
{
    grid = std::make_unique<Grid> (*this);

    for (auto* b : { &inputsTab, &outputsTab })
    {
        addAndMakeVisible (b);
        b->setClickingTogglesState (false);
    }

    inputsTab.onClick  = [this] { outputs = false; page = 0; refresh(); };
    outputsTab.onClick = [this] { outputs = true;  page = 0; refresh(); };

    help.setColour (juce::Label::textColourId, Theme::textDim);
    help.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (help);

    viewport.setViewedComponent (grid.get(), false);
    viewport.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport);

    setSize (1300, 720);
    refresh();
}

RoutingMatrix::~RoutingMatrix() = default;

void RoutingMatrix::setSelectedRack (int rack)
{
    if (rack != selectedRack)
    {
        selectedRack = rack;
        selectedSide = 0;
        const auto cfg = engine.getRackConfig (rack);
        driver = juce::jlimit (0, engine.getNumDrivers() - 1, cfg.driver);
        const int ch = outputs ? cfg.output[0] : cfg.input[0];
        if (ch >= 0)
            page = ch / perPage;
        refresh();
    }
}

void RoutingMatrix::updateTabs()
{
    driverTabs.clear();
    for (int d = 0; d < engine.getNumDrivers(); ++d)
    {
        auto* b = driverTabs.add (new juce::TextButton (engine.getDriver (d).label));
        b->setToggleState (d == driver, juce::dontSendNotification);
        b->onClick = [this, d] { driver = d; page = 0; refresh(); };
        addAndMakeVisible (b);
    }

    const auto status = engine.getDriver (driver);
    const int channels = juce::jmax (perPage, outputs ? status.numOuts : status.numIns);
    const int pages = (channels + perPage - 1) / perPage;
    page = juce::jlimit (0, pages - 1, page);

    pageTabs.clear();
    for (int p = 0; p < pages; ++p)
    {
        auto* b = pageTabs.add (new juce::TextButton ("Ch " + juce::String (p * perPage + 1) + "-" + juce::String (p * perPage + perPage)));
        b->setToggleState (p == page, juce::dontSendNotification);
        b->onClick = [this, p] { page = p; refresh(); };
        addAndMakeVisible (b);
    }

    inputsTab.setToggleState (! outputs, juce::dontSendNotification);
    outputsTab.setToggleState (outputs, juce::dontSendNotification);

    if (note.isNotEmpty())
    {
        help.setText (note, juce::dontSendNotification);
        help.setColour (juce::Label::textColourId, Theme::amber);
        return;
    }
    help.setColour (juce::Label::textColourId, Theme::textDim);
    help.setText (outputs ? "Rack outputs down the side, driver return channels across the top. Click a crosspoint to patch, click a lit one to unpatch. Red = two racks on one output (they add)."
                          : "Rack inputs down the side, driver channels across the top. Click a crosspoint to patch, click a lit one to unpatch. An unpatched output follows to the same number if it's free.",
                  juce::dontSendNotification);
}

void RoutingMatrix::refresh()
{
    updateTabs();
    grid->rebuild();
    resized();
}

void RoutingMatrix::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
}

void RoutingMatrix::resized()
{
    auto area = getLocalBounds().reduced (16);
    auto bar = area.removeFromTop (44);

    inputsTab.setBounds (bar.removeFromLeft (130));
    bar.removeFromLeft (4);
    outputsTab.setBounds (bar.removeFromLeft (130));
    bar.removeFromLeft (20);

    for (auto* b : driverTabs)
    {
        b->setBounds (bar.removeFromLeft (juce::jmin (230, juce::jmax (120, b->getBestWidthForHeight (44) + 20))));
        bar.removeFromLeft (6);
    }

    for (int i = pageTabs.size(); --i >= 0;)
    {
        pageTabs[i]->setBounds (bar.removeFromRight (90));
        bar.removeFromRight (4);
    }

    area.removeFromTop (8);
    help.setBounds (area.removeFromTop (22));
    area.removeFromTop (8);
    viewport.setBounds (area);
}
