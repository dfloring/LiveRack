#include "RackStripsView.h"
#include "GainReduction.h"
#include "PluginLibrary.h"
#include "Theme.h"
#include <map>

namespace
{
    const juce::Colour stripBack   { 0xff1a1d21 };
    const juce::Colour slotBack    { 0xff23272c };
    const juce::Colour meterBack   { 0xff0b0d0f };
    const juce::Colour bypassOn    { 0xffe0a030 };
    const juce::Colour safeOn      { 0xff3fb950 };
    const juce::Colour buttonOff   { 0xff23272c };

    juce::String arrow() { return juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92")); }

    juce::String chText (int c) { return c < 0 ? juce::String ("-") : juce::String (c + 1); }

    juce::String channelText (const int* ch, bool stereo)
    {
        if (! stereo)
            return chText (ch[0]);
        if (ch[0] >= 0 && ch[1] == ch[0] + 1)
            return chText (ch[0]) + "-" + chText (ch[1]);
        return chText (ch[0]) + "/" + chText (ch[1]);
    }

    // Colour swatches inside a popup menu
    class SwatchGrid : public juce::PopupMenu::CustomComponent
    {
    public:
        SwatchGrid (int current, std::function<void (int)> cb) : CustomComponent (false), selected (current), onPick (std::move (cb)) {}

        void getIdealSize (int& w, int& h) override { w = 4 * 44 + 12; h = 2 * 44 + 12 + 34; }

        juce::Rectangle<int> cell (int i) const { return { 6 + (i % 4) * 44, 6 + (i / 4) * 44, 40, 40 }; }
        juce::Rectangle<int> autoCell() const   { return { 6, 6 + 2 * 44 + 2, 4 * 44 - 4, 30 }; }

        void paint (juce::Graphics& g) override
        {
            for (int i = 0; i < RackStripsView::numColours; ++i)
            {
                auto r = cell (i).toFloat();
                g.setColour (RackStripsView::colourForIndex (i));
                g.fillRoundedRectangle (r.reduced (3.0f), 7.0f);
                if (i == selected)
                {
                    g.setColour (juce::Colours::white);
                    g.drawRoundedRectangle (r.reduced (1.0f), 8.0f, 2.0f);
                }
            }

            auto a = autoCell().toFloat();
            g.setColour (selected < 0 ? juce::Colour (0xff24364c) : juce::Colour (0xff2b3036));
            g.fillRoundedRectangle (a, 6.0f);
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText ("Automatic", a.toNearestInt(), juce::Justification::centred);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            int pick = -2;
            for (int i = 0; i < RackStripsView::numColours; ++i)
                if (cell (i).contains (e.getPosition())) pick = i;
            if (autoCell().contains (e.getPosition())) pick = -1;

            if (pick != -2)
            {
                auto cb = onPick;
                juce::PopupMenu::dismissAllActiveMenus();
                if (cb) cb (pick);
            }
        }

    private:
        int selected;
        std::function<void (int)> onPick;
    };
}

juce::Colour RackStripsView::colourForIndex (int index)
{
    static const juce::uint32 palette[numColours] = { 0xff4a90d9, 0xffe0a63a, 0xff52b788, 0xffb07cd8,
                                                      0xff3bb3c3, 0xffe06c6c, 0xffd4c34a, 0xffe58fb8 };
    return juce::Colour (palette[(size_t) juce::jmax (0, index) % numColours]);
}

juce::String RackStripsView::colourName (int index)
{
    static const char* names[numColours] = { "Blue", "Amber", "Green", "Purple", "Teal", "Red", "Yellow", "Pink" };
    return names[(size_t) juce::jmax (0, index) % numColours];
}

juce::Colour RackStripsView::colourFor (const RackEngine& engine, int rack)
{
    return colourForIndex (engine.getRackConfig (rack).effectiveColour (rack));
}

//==============================================================================
class RackStripsView::Content : public juce::Component,
                                public juce::DragAndDropTarget
{
public:
    Content (RackStripsView& o, RackEngine& e) : owner (o), engine (e) {}

    // Drag and drop of plugins
    int dragFromRack = -1, dragFromSlot = -1;
    bool dragStarted = false;
    int dropRack = -1, dropIndex = -1;

    static constexpr int stripWidth = 160, gap = 10, margin = 8, minRowHeight = 380, maxRows = 3;

    bool stackRows = true;
    int rows = 1, cols = 1;

    // Decide the grid for the current number of racks and available height
    void updateGrid (int availableWidth, int availableHeight)
    {
        const int cells = count() + 1; // +1 for the "Add Rack" strip
        const int fitCols = juce::jmax (1, (availableWidth - 2 * margin + gap) / (stripWidth + gap));
        rows = 1;

        // Only stack when the racks don't already fit across, and only as
        // many rows as the height allows
        if (stackRows && cells > fitCols)
        {
            const int rowsByHeight = juce::jlimit (1, maxRows, (availableHeight - 2 * margin + gap) / (minRowHeight + gap));
            rows = juce::jmin (rowsByHeight, (cells + fitCols - 1) / fitCols);
        }

        cols = (cells + rows - 1) / rows;
    }
    static constexpr int headerH = 34, badgeH = 28, ioH = 22, slotH = 28, slotGap = 4, footerH = 26, latencyH = 18, meterW = 6;

    int selectedRack = -1, selectedSlot = -1;
    std::vector<std::array<float, 2>> levels; // per rack: input, output
    struct MeterHold { float level[2] {}; int ticks[2] {}, clipTicks[2] {}; };
    std::vector<MeterHold> holds;             // peak hold and clip, per rack

    PluginLibrary* library = nullptr;

    // Gain-reduction state per plugin (cleared whenever racks change)
    struct SlotMeter
    {
        bool probed = false;
        GainReduction::Probe probe;
        bool estimateAllowed = false;
        float inEnv = 0.0f, outEnv = 0.0f; // level into / out of the plugin
        float gr = 0.0f;                   // shown value, dB (or GainReduction::muted)
        bool show = false, estimated = false;
    };
    std::map<juce::AudioProcessor*, SlotMeter> slotMeters;

    SlotMeter& meterFor (juce::AudioPluginInstance& plugin, int rack, int slot)
    {
        auto& m = slotMeters[&plugin];

        if (! m.probed)
        {
            const auto desc = engine.getSlotDescription (rack, slot);
            const auto key = PluginLibrary::familyKey (desc);
            const int userIndex = library != nullptr ? library->getGainReductionParameterOverride (key) : -1;

            m.probe = GainReduction::probe (plugin, userIndex);
            m.estimateAllowed = library != nullptr && library->getShowEstimatedGR (key, library->categoryFor (desc));
            m.probed = true;
        }

        return m;
    }

    struct Layout
    {
        juce::Rectangle<int> strip, header, chBadge, io, addRow, bypass, safe, latency, meterIn, meterOut;
        juce::Rectangle<int> formatBadge, groupBadge, safeBadge, swatch;
        std::vector<juce::Rectangle<int>> slots;
        bool overflow = false;
    };

    int contentWidth() const { return margin * 2 + cols * (stripWidth + gap); }

    // The racks shown (the current page), in order
    std::vector<int> order;
    static constexpr int addStrip = -2;

    int count() const { return (int) order.size(); }

    int posOf (int rack) const
    {
        for (int i = 0; i < (int) order.size(); ++i)
            if (order[(size_t) i] == rack)
                return i;
        return -1;
    }

    juce::Rectangle<int> cellBounds (int pos) const
    {
        const int rowHeight = (getHeight() - 2 * margin - (rows - 1) * gap) / juce::jmax (1, rows);
        const int row = pos / juce::jmax (1, cols), col = pos % juce::jmax (1, cols);
        return { margin + col * (stripWidth + gap), margin + row * (rowHeight + gap), stripWidth, rowHeight };
    }

    juce::Rectangle<int> stripBounds (int rack) const
    {
        const int pos = posOf (rack);
        return pos >= 0 ? cellBounds (pos) : juce::Rectangle<int>();
    }

    Layout layoutFor (int rack) const
    {
        Layout l;
        l.strip = stripBounds (rack);

        auto area = l.strip.reduced (7);
        l.header = area.removeFromTop (headerH);
        l.chBadge = l.header.reduced (6, 7).removeFromRight (46);
        area.removeFromTop (6);
        auto badges = area.removeFromTop (badgeH);
        l.formatBadge = badges.removeFromLeft (42); badges.removeFromLeft (4);
        l.groupBadge  = badges.removeFromLeft (28); badges.removeFromLeft (4);
        l.safeBadge   = badges.removeFromLeft (40); badges.removeFromLeft (4);
        l.swatch      = badges.removeFromLeft (juce::jmin (28, badges.getWidth()));
        l.io = area.removeFromTop (ioH);

        auto footer = area.removeFromBottom (footerH);
        l.bypass = footer.removeFromLeft (footer.getWidth() / 2 - 2);
        footer.removeFromLeft (4);
        l.safe = footer;

        area.removeFromBottom (4);
        l.latency = area.removeFromBottom (latencyH);
        area.removeFromBottom (2);

        l.meterOut = area.removeFromRight (meterW);
        area.removeFromRight (2);
        l.meterIn = area.removeFromRight (meterW);
        area.removeFromRight (6);

        const int n = engine.getNumSlots (rack);

        for (int s = 0; s < n; ++s)
        {
            if (area.getHeight() < slotH * 2 + slotGap) // keep room for the add row
            {
                l.overflow = true;
                break;
            }

            l.slots.push_back (area.removeFromTop (slotH));
            area.removeFromTop (slotGap);
        }

        if (area.getHeight() >= slotH)
            l.addRow = area.removeFromTop (slotH);

        return l;
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        for (int r : order)
            paintStrip (g, r);

        // "Add Rack" placeholder strip
        auto add = cellBounds (count()).toFloat().reduced (1.0f);
        juce::Path outline, dashed;
        outline.addRoundedRectangle (add, 8.0f);
        const float dashes[] = { 6.0f, 5.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.fillPath (dashed);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("+  Add Rack", add.toNearestInt(), juce::Justification::centred);

        paintDropIndicator (g);
    }

    void paintStrip (juce::Graphics& g, int r)
    {
        const auto l = layoutFor (r);
        const auto cfg = engine.getRackConfig (r);
        const bool bypassed = engine.isRackBypassed (r);
        const bool isSelected = r == selectedRack;
        const auto colour = colourForIndex (cfg.effectiveColour (r));

        // Strip body, its colour along the top, and the selection outline
        auto body = l.strip.toFloat();
        g.setColour (stripBack);
        g.fillRoundedRectangle (body, 8.0f);
        {
            juce::Graphics::ScopedSaveState save (g);
            juce::Path clip;
            clip.addRoundedRectangle (body, 8.0f);
            g.reduceClipRegion (clip);
            g.setColour (colour.withAlpha (bypassed ? 0.4f : 1.0f));
            g.fillRect (body.withHeight (4.0f));
        }

        g.setColour (isSelected ? juce::Colour (0xff5a636e) : Theme::border);
        g.drawRoundedRectangle (body.reduced (0.5f), 8.0f, isSelected ? 1.5f : 1.0f);

        // Header: number, name, console channel
        auto text = l.header.reduced (6, 0).withTrimmedTop (2);
        text.removeFromRight (l.chBadge.getWidth() + 4);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (juce::String (r + 1), text.removeFromLeft (18), juce::Justification::centredLeft);
        g.setColour (bypassed ? Theme::textDim : Theme::text);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawFittedText (cfg.name, text, juce::Justification::centredLeft, 1, 0.75f);

        g.setColour (Theme::raised);
        g.fillRoundedRectangle (l.chBadge.toFloat().withTrimmedTop (2.0f), 5.0f);
        g.setColour (Theme::textDim.brighter (0.2f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (cfg.consoleChannel > 0 ? "CH " + juce::String (cfg.consoleChannel) : juce::String ("CH -"),
                    l.chBadge.withTrimmedTop (2), juce::Justification::centred);

        // Badges: format, latency group, recall safe, colour
        paintBadge (g, l.formatBadge, cfg.formatName(), cfg.isMonoToStereo(), Theme::accent.withAlpha (0.8f), false);
        if (cfg.latencyGroup > 0)
            paintBadge (g, l.groupBadge, RackConfig::groupName (cfg.latencyGroup), true, Theme::groupColour (cfg.latencyGroup), false);
        else
            paintBadge (g, l.groupBadge, "-", false, {}, true);
        paintBadge (g, l.safeBadge, "SAFE", cfg.recallSafe, Theme::amber, false);
        g.setColour (colour);
        g.fillRoundedRectangle (l.swatch.toFloat().reduced (2.0f), 5.0f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawRoundedRectangle (l.swatch.toFloat().reduced (1.0f), 6.0f, 1.0f);

        // I/O line (click to open Routing)
        g.setColour (juce::Colours::white.withAlpha (0.62f));
        g.setFont (juce::FontOptions (12.0f));
        juce::String io;
        if (engine.getNumDrivers() > 1)
            io << "D" << (cfg.driver + 1) << "  ";
        io << channelText (cfg.input, cfg.numInputs() == 2) << " " << arrow() << " " << channelText (cfg.output, cfg.numOutputs() == 2);
        g.drawText (io, l.io, juce::Justification::centred, true);

        // Plugin slots
        for (int s = 0; s < (int) l.slots.size(); ++s)
            paintSlot (g, r, s, l.slots[(size_t) s], colour, bypassed);

        if (! l.addRow.isEmpty())
        {
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.drawRoundedRectangle (l.addRow.toFloat().reduced (0.5f), 5.0f, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.45f));
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (l.overflow ? "more..." : "+  Add plugin", l.addRow, juce::Justification::centred);
        }

        // Meters
        const auto lv = juce::isPositiveAndBelow (r, (int) levels.size()) ? levels[(size_t) r]
                                                                             : std::array<float, 2> { 0.0f, 0.0f };
        const auto hd = juce::isPositiveAndBelow (r, (int) holds.size()) ? holds[(size_t) r] : MeterHold {};
        paintMeter (g, l.meterIn, lv[0], hd.level[0], hd.clipTicks[0] > 0);
        paintMeter (g, l.meterOut, lv[1], hd.level[1], hd.clipTicks[1] > 0);

        // Latency: the rack's total, and its group's
        const double ms = engine.getRackTotalLatencyMs (r);
        juce::String latText = ms > 0.005 ? "+" + juce::String (ms, 2) + " ms" : juce::String ("no added latency");
        if (cfg.latencyGroup > 0)
            latText = "group " + RackConfig::groupName (cfg.latencyGroup) + "  " + latText;
        g.setColour (cfg.latencyGroup > 0 ? Theme::groupColour (cfg.latencyGroup).withAlpha (0.9f) : juce::Colours::white.withAlpha (0.45f));
        g.setFont (juce::FontOptions (11.5f));
        auto latArea = l.latency;

        // Processing time: the longest block in the last few seconds, as a share of the block
        if (! bypassed)
        {
            const auto load = engine.getRackLoad (r).peak;
            const auto pc = (int) std::ceil (load * 100.0f);
            const auto loadArea = latArea.removeFromRight (30);
            g.setColour (load > 0.5f ? Theme::red : load > 0.25f ? Theme::amber : juce::Colours::white.withAlpha (0.35f));
            g.drawText (pc <= 1 ? juce::String ("<1%") : juce::String (pc) + "%", loadArea, juce::Justification::centredRight);
            g.setColour (cfg.latencyGroup > 0 ? Theme::groupColour (cfg.latencyGroup).withAlpha (0.9f) : juce::Colours::white.withAlpha (0.45f));
        }
        g.drawFittedText (latText, latArea, juce::Justification::centred, 1, 0.85f);

        // Buttons
        paintButton (g, l.bypass, "BYPASS", bypassed, bypassOn);
        paintButton (g, l.safe, "SAFE", cfg.recallSafe, safeOn);
    }

    void paintSlot (juce::Graphics& g, int r, int s, juce::Rectangle<int> area, juce::Colour colour, bool rackBypassed)
    {
        auto* plugin = engine.getPlugin (r, s);
        const bool slotBypassed = engine.isSlotBypassed (r, s);
        const bool isSelected = r == selectedRack && s == selectedSlot;
        const float dim = (rackBypassed || slotBypassed) ? 0.45f : 1.0f;

        g.setColour (slotBack.withAlpha (dim));
        g.fillRoundedRectangle (area.toFloat(), 5.0f);

        if (isSelected)
        {
            g.setColour (Theme::accent);
            g.drawRoundedRectangle (area.toFloat().reduced (0.75f), 5.0f, 1.5f);
        }
        else
        {
            g.setColour (Theme::border);
            g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 5.0f, 1.0f);
        }
        juce::ignoreUnused (colour);

        auto text = area.reduced (8, 0);

        // Mono-to-stereo racks: mark where the chain turns stereo
        if (engine.getStereoPoint (r) == s)
        {
            auto tag = area.reduced (3, 6).removeFromLeft (4).toFloat();
            g.setColour (Theme::accent);
            g.fillRoundedRectangle (tag, 2.0f);
            text.removeFromLeft (2);
        }

        if (slotBypassed)
        {
            g.setColour (bypassOn);
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText ("BYP", text.removeFromRight (26), juce::Justification::centredRight);
        }
        else if (auto it = slotMeters.find (plugin); it != slotMeters.end() && it->second.show)
        {
            // Gain reduction: a bar growing from the right along the slot's
            // bottom edge (full width = 20 dB) and the value in dB.
            // Estimates are paler and marked with "~".
            const auto& m = it->second;
            const bool isMuted = std::isinf (m.gr);
            const bool active = isMuted || m.gr > 0.05f;
            const auto grColour = m.estimated ? juce::Colour (0xffc8a878) : juce::Colour (0xffe8a23a);

            const auto bar = area.reduced (4, 0).removeFromBottom (4).withTrimmedBottom (1);
            g.setColour (meterBack);
            g.fillRect (bar);

            if (active)
            {
                const float fraction = isMuted ? 1.0f : juce::jlimit (0.0f, 1.0f, m.gr / 20.0f);
                const int w = juce::roundToInt (bar.getWidth() * fraction);
                g.setColour (grColour);
                g.fillRect (bar.withLeft (bar.getRight() - juce::jmax (1, w)));
            }

            juce::String label = m.estimated ? "~GR" : "GR";
            if (isMuted)     label = m.estimated ? "~-inf" : "-inf";
            else if (active) label = (m.estimated ? "~-" : "-") + juce::String (m.gr, 1);

            g.setColour (grColour.withAlpha (active ? 1.0f : 0.45f));
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (label, text.removeFromRight (isMuted || m.estimated ? 38 : 32).withTrimmedBottom (3),
                        juce::Justification::centredRight);
        }

        g.setColour (juce::Colours::white.withAlpha (0.92f * dim));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (plugin != nullptr ? plugin->getName() : "?", text, juce::Justification::centredLeft, true);
    }

    // Segmented meter, -60 to 0 dB: green, amber from -18, red from -6;
    // a peak-hold line, and a clip light above it
    static void paintMeter (juce::Graphics& g, juce::Rectangle<int> full, float level, float hold, bool clipped)
    {
        auto clipLight = full.removeFromTop (full.getWidth()).toFloat();
        full.removeFromTop (2);
        g.setColour (clipped ? Theme::red : meterBack);
        g.fillRoundedRectangle (clipLight, 1.0f);

        const auto area = full.toFloat();
        g.setColour (meterBack);
        g.fillRect (area);

        auto norm = [] (float gain) { return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (gain, -60.0f) + 60.0f) / 60.0f); };
        auto zone = [] (float n) { return n >= 0.9f ? Theme::red : n >= 0.7f ? Theme::amber : Theme::green; };

        const float lit = norm (level);
        const float seg = 3.0f, gap = 1.0f;

        for (float y = area.getBottom() - seg; y >= area.getY(); y -= seg + gap)
        {
            const float n = (area.getBottom() - (y + seg * 0.5f)) / area.getHeight();
            g.setColour (n <= lit ? zone (n) : zone (n).withAlpha (0.07f));
            g.fillRect (area.getX(), y, area.getWidth(), seg);
        }

        if (const float h = norm (hold); h > 0.0f)
        {
            g.setColour (Theme::text);
            g.fillRect (area.getX(), area.getBottom() - area.getHeight() * h, area.getWidth(), 1.5f);
        }
    }

    static void paintBadge (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label,
                            bool on, juce::Colour onColour, bool dashed)
    {
        const auto r = area.toFloat().reduced (0.5f);
        if (dashed)
        {
            juce::Path outline, d;
            outline.addRoundedRectangle (r, 5.0f);
            const float dashes[] = { 3.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (d, outline, dashes, 2);
            g.setColour (juce::Colours::white.withAlpha (0.3f));
            g.fillPath (d);
        }
        else
        {
            g.setColour (on ? onColour : juce::Colour (0xff22262b));
            g.fillRoundedRectangle (r, 5.0f);
            if (! on)
            {
                g.setColour (juce::Colour (0xff3a4048));
                g.drawRoundedRectangle (r, 5.0f, 1.0f);
            }
        }

        g.setColour (on ? juce::Colour (0xff101214) : juce::Colours::white.withAlpha (dashed ? 0.5f : 0.75f));
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (label, area, juce::Justification::centred);
    }

    static void paintButton (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label, bool on, juce::Colour onColour)
    {
        g.setColour (on ? onColour : buttonOff);
        g.fillRoundedRectangle (area.toFloat(), 5.0f);
        g.setColour (on ? juce::Colours::black.withAlpha (0.85f) : juce::Colours::white.withAlpha (0.6f));
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (label, area, juce::Justification::centred);
    }

    //==========================================================================
    int rackAt (juce::Point<int> pos) const
    {
        for (int i = 0; i < count(); ++i)
            if (cellBounds (i).contains (pos))
                return order[(size_t) i];

        return cellBounds (count()).contains (pos) ? addStrip : -1;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto pos = e.getPosition();
        const int r = rackAt (pos);

        if (r == -1)
            return;

        if (r == addStrip)
        {
            if (owner.onAddRack != nullptr) owner.onAddRack();
            return;
        }

        const auto l = layoutFor (r);
        const bool popup = e.mods.isPopupMenu();

        for (int s = 0; s < (int) l.slots.size(); ++s)
        {
            if (l.slots[(size_t) s].contains (pos))
            {
                if (! popup)
                {
                    dragFromRack = r;
                    dragFromSlot = s;
                    dragStarted = false;
                }

                if (owner.onSelect != nullptr)   owner.onSelect (r, s);
                if (popup && owner.onSlotMenu != nullptr) owner.onSlotMenu (r, s);
                return;
            }
        }

        if (l.addRow.contains (pos) && ! popup)
        {
            if (owner.onSelect != nullptr)    owner.onSelect (r, -1);
            if (owner.onAddPlugin != nullptr) owner.onAddPlugin (r);
            return;
        }

        if (! popup)
        {
            if (l.formatBadge.contains (pos))
            {
                // MONO > M>S > ST > MONO
                auto cfg = engine.getRackConfig (r);
                if (cfg.stereo)                 { cfg.stereo = false; cfg.monoToStereo = false; }
                else if (cfg.monoToStereo)      { cfg.stereo = true;  cfg.monoToStereo = false; }
                else                            { cfg.monoToStereo = true; }
                if (cfg.numOutputs() == 2)
                {
                    if (cfg.input[1] < 0 || cfg.input[1] == cfg.input[0])   cfg.input[1] = cfg.input[0] + 1;
                    if (cfg.output[1] < 0 || cfg.output[1] == cfg.output[0]) cfg.output[1] = cfg.output[0] + 1;
                }
                if (owner.onFormatChange != nullptr) owner.onFormatChange (r, cfg);
                return;
            }
            if (l.groupBadge.contains (pos))  { showGroupMenu (r); return; }
            if (l.safeBadge.contains (pos))   { engine.setRackRecallSafe (r, ! engine.getRackConfig (r).recallSafe); return; }
            if (l.swatch.contains (pos))      { showColourMenu (r, l.swatch); return; }
            if (l.chBadge.contains (pos))     { if (owner.onEditConsoleChannel != nullptr) owner.onEditConsoleChannel (r); return; }
            if (l.io.contains (pos))
            {
                if (owner.onSelect != nullptr)      owner.onSelect (r, -1);
                if (owner.onOpenRouting != nullptr) owner.onOpenRouting (r);
                return;
            }
        }

        if (l.bypass.contains (pos)) { engine.setRackBypassed (r, ! engine.isRackBypassed (r)); return; }
        if (l.safe.contains (pos))   { engine.setRackRecallSafe (r, ! engine.getRackConfig (r).recallSafe); return; }

        if (owner.onSelect != nullptr) owner.onSelect (r, -1);
        if (popup && owner.onRackMenu != nullptr) owner.onRackMenu (r);
    }

    void showGroupMenu (int r)
    {
        const int current = engine.getRackConfig (r).latencyGroup;
        juce::PopupMenu m;
        m.addSectionHeader ("Latency group");
        m.addItem (100, "None (lowest latency)", true, current == 0);
        for (int g = 1; g <= 8; ++g)
        {
            juce::String text = "Group " + RackConfig::groupName (g);
            const double ms = engine.getGroupLatencyMs (g);
            if (ms > 0.0)
                text << "   (" << juce::String (ms, 2) << " ms)";
            m.addColouredItem (100 + g, text, Theme::groupColour (g), true, current == g);
        }

        juce::Component::SafePointer<Content> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (layoutFor (r).groupBadge)), [safe, r] (int result)
        {
            if (safe != nullptr && result >= 100)
                safe->engine.setRackLatencyGroup (r, result - 100);
        });
    }

    void showColourMenu (int r, juce::Rectangle<int> area)
    {
        juce::Component::SafePointer<Content> safe (this);
        juce::PopupMenu m;
        m.addCustomItem (1, std::make_unique<SwatchGrid> (engine.getRackConfig (r).colourIndex, [safe, r] (int colour)
        {
            if (safe != nullptr)
                safe->engine.setRackColour (r, colour);
        }), nullptr, "Colour");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (area)));
    }

    // Double-click the name to rename in place
    void startRename (int r)
    {
        renameRack = r;
        renameEditor = std::make_unique<juce::TextEditor>();
        auto& ed = *renameEditor;
        ed.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        ed.setText (engine.getRackConfig (r).name, false);
        ed.selectAll();
        ed.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff101214));
        ed.setColour (juce::TextEditor::textColourId, juce::Colours::white);
        ed.setColour (juce::TextEditor::outlineColourId, juce::Colours::white);
        ed.onReturnKey = [this] { finishRename (true); };
        ed.onEscapeKey = [this] { finishRename (false); };
        ed.onFocusLost = [this] { finishRename (true); };
        addAndMakeVisible (ed);
        auto h = layoutFor (r).header;
        ed.setBounds (h.withTrimmedRight (50).reduced (2));
        ed.grabKeyboardFocus();
    }

    void finishRename (bool keep)
    {
        if (renameEditor == nullptr)
            return;

        const auto text = renameEditor->getText().trim();
        const int r = renameRack;
        juce::Component::SafePointer<Content> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->renameEditor.reset(); });
        renameEditor->onFocusLost = nullptr;
        renameEditor->setVisible (false);

        if (keep && text.isNotEmpty())
            engine.setRackName (r, text);
    }

    std::unique_ptr<juce::TextEditor> renameEditor;
    int renameRack = -1;

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragFromSlot < 0 || dragStarted || e.getDistanceFromDragStart() < 6)
            return;

        const auto l = layoutFor (dragFromRack);
        if (! juce::isPositiveAndBelow (dragFromSlot, (int) l.slots.size()))
            return;

        dragStarted = true;
        const auto image = createComponentSnapshot (l.slots[(size_t) dragFromSlot]);
        owner.startDragging ("slot:" + juce::String (dragFromRack) + ":" + juce::String (dragFromSlot),
                             this, juce::ScaledImage (image), true);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragFromRack = dragFromSlot = -1;
        dragStarted = false;
    }

    // Where a drop at this position would insert: rack and index
    std::pair<int, int> dropTargetAt (juce::Point<int> pos) const
    {
        const int r = rackAt (pos);
        if (! juce::isPositiveAndBelow (r, engine.getNumRacks()))
            return { -1, -1 };

        const auto l = layoutFor (r);
        int index = 0;
        for (auto& slot : l.slots)
            if (pos.y > slot.getCentreY())
                ++index;

        return { r, index };
    }

    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        const auto t = d.description.toString();
        return t.startsWith ("slot:") || t.startsWith ("plugin:");
    }

    void itemDragMove (const SourceDetails& d) override
    {
        const auto [r, i] = dropTargetAt (d.localPosition);
        if (r != dropRack || i != dropIndex)
        {
            dropRack = r;
            dropIndex = i;
            repaint();
        }
    }

    void itemDragExit (const SourceDetails&) override
    {
        dropRack = dropIndex = -1;
        repaint();
    }

    void itemDropped (const SourceDetails& d) override
    {
        const auto [r, i] = dropTargetAt (d.localPosition);
        dropRack = dropIndex = -1;
        repaint();

        const auto text = d.description.toString();
        if (text.startsWith ("plugin:"))
        {
            if (r >= 0 && owner.onPluginDropped != nullptr)
                owner.onPluginDropped (text.fromFirstOccurrenceOf ("plugin:", false, false), r, i);
            return;
        }

        juce::StringArray parts;
        parts.addTokens (text, ":", "");

        if (r < 0 || parts.size() != 3 || owner.onSlotDropped == nullptr)
            return;

        owner.onSlotDropped (parts[1].getIntValue(), parts[2].getIntValue(), r, i,
                             juce::ModifierKeys::currentModifiers.isAltDown());
    }

    void paintDropIndicator (juce::Graphics& g)
    {
        if (dropRack < 0)
            return;

        const auto l = layoutFor (dropRack);
        int y = l.addRow.isEmpty() ? l.strip.getCentreY() : l.addRow.getY() - 2;

        if (dropIndex < (int) l.slots.size())
            y = l.slots[(size_t) dropIndex].getY() - 2;
        else if (! l.slots.empty())
            y = l.slots.back().getBottom() + 1;

        const auto x = l.slots.empty() ? l.addRow.getX() : l.slots.front().getX();
        const auto w = l.slots.empty() ? l.addRow.getWidth() : l.slots.front().getWidth();

        g.setColour (juce::Colours::white);
        g.fillRoundedRectangle ((float) x, (float) y - 1.0f, (float) w, 3.0f, 1.5f);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int r = rackAt (e.getPosition());

        if (juce::isPositiveAndBelow (r, engine.getNumRacks()))
        {
            const auto l = layoutFor (r);
            if (l.header.contains (e.getPosition()) && ! l.chBadge.contains (e.getPosition()))
                startRename (r);

            for (int s = 0; s < (int) l.slots.size() && s < engine.getNumSlots (r); ++s)
                if (l.slots[(size_t) s].contains (e.getPosition()) && owner.onOpenSlot != nullptr)
                    owner.onOpenSlot (r, s);
        }
    }

    RackStripsView& owner;
    RackEngine& engine;
};

//==============================================================================
RackStripsView::RackStripsView (RackEngine& e)
    : content (std::make_unique<Content> (*this, e))
{
    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (8);
    viewport.getHorizontalScrollBar().setColour (juce::ScrollBar::thumbColourId, juce::Colours::white.withAlpha (0.25f));
    addAndMakeVisible (viewport);
}

RackStripsView::~RackStripsView() = default;

void RackStripsView::resized()
{
    viewport.setBounds (getLocalBounds());
    refresh();
}

void RackStripsView::setLibrary (PluginLibrary* lib)
{
    content->library = lib;
    forgetMeters();
}

void RackStripsView::forgetMeters()
{
    content->slotMeters.clear();
}

void RackStripsView::setStackRows (bool shouldStack)
{
    content->stackRows = shouldStack;
    refresh();
}

bool RackStripsView::getStackRows() const { return content->stackRows; }

void RackStripsView::refresh()
{
    // Plugins may have been added or removed: look the GR meters up again
    content->slotMeters.clear();
    content->order = content->engine.getVisibleRacks();

    content->updateGrid (getWidth(), getHeight() - viewport.getScrollBarThickness());

    const int w = content->contentWidth();
    const bool scrolls = w > getWidth();
    content->setSize (juce::jmax (w, getWidth()), getHeight() - (scrolls ? viewport.getScrollBarThickness() : 0));
    content->repaint();
}

void RackStripsView::setSelection (int rack, int slot)
{
    content->selectedRack = rack;
    content->selectedSlot = slot;
    content->repaint();
}

void RackStripsView::updateMeters()
{
    auto& engine = content->engine;
    const int n = engine.getNumRacks();
    content->levels.resize ((size_t) n, { 0.0f, 0.0f });
    content->holds.resize ((size_t) n);

    for (int r = 0; r < n; ++r)
    {
        float rackInput = 0.0f;

        for (int io = 0; io < 2; ++io)
        {
            const float peak = juce::jmax (engine.consumePeak (r, io == 1, 0), engine.consumePeak (r, io == 1, 1));
            auto& level = content->levels[(size_t) r][(size_t) io];
            level = juce::jmax (peak, level * 0.82f); // fast attack, smooth fall

            // Peak hold for 1.5 s, clip light for 3 s (called 30 times a second)
            auto& h = content->holds[(size_t) r];
            if (peak >= h.level[io])         { h.level[io] = peak; h.ticks[io] = 45; }
            else if (--h.ticks[io] <= 0)     h.level[io] *= 0.9f;
            if (peak >= 0.999f)              h.clipTicks[io] = 90;
            else if (h.clipTicks[io] > 0)    --h.clipTicks[io];

            if (io == 0)
                rackInput = peak;
        }

        float intoSlot = rackInput; // the level going into each plugin

        for (int s = 0; s < engine.getNumSlots (r); ++s)
        {
            auto* plugin = engine.getPlugin (r, s);
            const float outOfSlot = engine.consumeSlotOutputPeak (r, s);

            if (plugin == nullptr)
                continue;

            auto& m = content->meterFor (*plugin, r, s);
            m.inEnv  = juce::jmax (intoSlot, m.inEnv * 0.85f);
            m.outEnv = juce::jmax (outOfSlot, m.outEnv * 0.85f);
            intoSlot = outOfSlot;

            float gr = 0.0f;

            if (m.probe.source != GainReduction::Source::none && GainReduction::read (*plugin, m.probe, gr))
            {
                m.show = true;
                m.estimated = false;
            }
            else if (m.estimateAllowed && ! engine.isSlotBypassed (r, s))
            {
                // No GR reported: estimate from the level drop across the plugin.
                // Makeup gain hides some reduction, hence the "~".
                m.show = true;
                m.estimated = true;
                const float inDb = juce::Decibels::gainToDecibels (m.inEnv, -100.0f);
                const float outDb = juce::Decibels::gainToDecibels (m.outEnv, -100.0f);
                if (inDb <= -50.0f)       gr = 0.0f;                       // too quiet to judge
                else if (outDb <= -90.0f) gr = GainReduction::muted;       // e.g. a closed gate
                else                      gr = juce::jlimit (0.0f, 60.0f, inDb - outDb);
            }
            else
            {
                m.show = false;
                continue;
            }

            // 60 dB or more of reduction (a closed gate, say) reads as -inf
            if (gr >= 60.0f)
                gr = GainReduction::muted;

            if (std::isinf (gr) || std::isinf (m.gr))
                m.gr = gr;                                                // straight to / from -inf
            else
                m.gr = gr > m.gr ? gr : m.gr * 0.7f + gr * 0.3f;         // fast attack, smooth release
        }
    }

    content->repaint();
}

#if LIVERACK_PREVIEW_HOOKS
void RackStripsView::previewMenu (int which, int rack)
{
    if (which == 0) content->showGroupMenu (rack);
    if (which == 1) content->showColourMenu (rack, content->layoutFor (rack).swatch);
    if (which == 2) content->startRename (rack);
}
#endif

float RackStripsView::getLevel (int rack, bool output) const
{
    if (! juce::isPositiveAndBelow (rack, (int) content->levels.size()))
        return 0.0f;
    const float db = juce::Decibels::gainToDecibels (content->levels[(size_t) rack][output ? 1 : 0], -60.0f);
    return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
}

float RackStripsView::getOutputLevel (int rack) const
{
    if (! juce::isPositiveAndBelow (rack, (int) content->levels.size()))
        return 0.0f;
    const float db = juce::Decibels::gainToDecibels (content->levels[(size_t) rack][1], -60.0f);
    return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
}

float RackStripsView::getMaxGainReduction (int rack) const
{
    float gr = 0.0f;
    auto& engine = content->engine;
    for (int s = 0; s < engine.getNumSlots (rack); ++s)
        if (auto it = content->slotMeters.find (engine.getPlugin (rack, s)); it != content->slotMeters.end() && it->second.show)
            gr = juce::jmax (gr, std::isinf (it->second.gr) ? 60.0f : it->second.gr);
    return gr;
}

void RackStripsView::scrollToRack (int rack)
{
    const auto b = content->stripBounds (rack);
    const auto view = viewport.getViewArea();

    if (b.getX() < view.getX())
        viewport.setViewPosition (juce::jmax (0, b.getX() - Content::margin), 0);
    else if (b.getRight() > view.getRight())
        viewport.setViewPosition (b.getRight() + Content::margin - view.getWidth(), 0);
}

juce::Rectangle<int> RackStripsView::getAddRowScreenBounds (int rack) const
{
    const auto l = content->layoutFor (rack);
    return content->localAreaToGlobal (l.addRow.isEmpty() ? l.strip : l.addRow);
}

juce::Rectangle<int> RackStripsView::getSlotScreenBounds (int rack, int slot) const
{
    const auto l = content->layoutFor (rack);

    if (! juce::isPositiveAndBelow (slot, (int) l.slots.size()))
        return {};

    return content->localAreaToGlobal (l.slots[(size_t) slot]);
}
