#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// LiveRack's colours and the look of buttons: rounded pills, lit when on,
// sized for touch screens
namespace Theme
{
    // Calm greys; colour only where it means something: accent = selected /
    // on, amber = bypass, red = clip, green = healthy
    const juce::Colour background { 0xff101215 };
    const juce::Colour panel      { 0xff181b1f };
    const juce::Colour raised     { 0xff23272c };
    const juce::Colour well       { 0xff121417 };   // sunken group behind toolbar buttons
    const juce::Colour border     { 0xff2a2f35 };
    const juce::Colour text       { 0xffe8eaed };
    const juce::Colour textDim    { 0xff9aa3ad };
    const juce::Colour accent     { 0xff4fb3d9 };
    const juce::Colour accentDeep { 0xff1f3a48 };
    const juce::Colour amber      { 0xffe0a030 };
    const juce::Colour green      { 0xff3fb950 };
    const juce::Colour red        { 0xffe5534b };

    // Latency group letters A-H
    inline juce::Colour groupColour (int g)
    {
        static const juce::uint32 c[8] = { 0xff4a90d9, 0xffe0a030, 0xff3aa9a0, 0xffb07cd8,
                                           0xffd96ba8, 0xff8bc34a, 0xffd9c94a, 0xff7f8c99 };
        return juce::Colour (c[(size_t) juce::jlimit (0, 7, g - 1)]);
    }

    inline void drawLight (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool glow = true)
    {
        if (glow)
        {
            g.setColour (c.withAlpha (0.25f));
            g.fillEllipse (r.expanded (3.0f));
        }
        g.setColour (c);
        g.fillEllipse (r);
    }

    // Toolbar icons, drawn in an 18 x 18 box with a 1.6 px stroke
    inline juce::Path icon (const juce::String& name)
    {
        juce::Path p;
        auto svg = [&p] (const char* d) { p.addPath (juce::Drawable::parseSVGPath (d)); };

        if (name == "save")      { svg ("M3 3h9l3 3v9H3z"); svg ("M6 3v4h5V3M6 15v-5h6v5"); }
        else if (name == "show") { svg ("M2 5h5l1.5 2H16v8H2z"); }
        else if (name == "prev") { svg ("M11 3L5 9l6 6"); }
        else if (name == "next") { svg ("M7 3l6 6-6 6"); }
        else if (name == "devices")
        {
            p.addRoundedRectangle (2.0f, 4.0f, 14.0f, 10.0f, 2.0f);
            for (float x : { 6.0f, 9.0f, 12.0f })
                p.addEllipse (x - 0.6f, 8.4f, 1.2f, 1.2f);
        }
        else if (name == "routing") { svg ("M4 3v12M9 3v12M14 3v12M2 6.5h14M2 11.5h14"); }
        else if (name == "plugins")
        {
            for (float x : { 2.5f, 10.0f })
                for (float y : { 2.5f, 10.0f })
                    p.addRoundedRectangle (x, y, 5.5f, 5.5f, 1.0f);
        }
        else if (name == "scope")   { svg ("M3 9.5l4 4 8-9"); p.addRoundedRectangle (1.5f, 1.5f, 15.0f, 15.0f, 3.0f); }
        else if (name == "addrack") { p.addRoundedRectangle (2.0f, 2.0f, 6.0f, 14.0f, 1.5f); svg ("M13 5v8M9 9h8"); }
        else if (name == "windows") { p.addRoundedRectangle (1.5f, 3.0f, 8.0f, 12.0f, 1.5f); p.addRoundedRectangle (11.0f, 3.0f, 5.5f, 12.0f, 1.5f); }
        else if (name == "snapshots") { p.addRoundedRectangle (1.5f, 3.0f, 11.0f, 8.0f, 1.5f); svg ("M5 14.5h11V6.5"); }
        else if (name == "rack")    { p.addRoundedRectangle (1.5f, 2.0f, 15.0f, 14.0f, 1.5f); svg ("M1.5 6h15M4.5 9.5h9M4.5 12.5h6"); }
        else if (name == "stack")   { p.addRoundedRectangle (2.0f, 2.0f, 14.0f, 6.0f, 1.5f); p.addRoundedRectangle (2.0f, 10.0f, 14.0f, 6.0f, 1.5f); }
        return p;
    }

    // A square toolbar button with an icon; lit with the accent when toggled on
    class IconButton : public juce::Button
    {
    public:
        IconButton (const juce::String& name, const juce::String& iconName, const juce::String& tip)
            : juce::Button (name), path (icon (iconName))
        {
            setTooltip (tip);
            setTitle (tip);
        }

        // A label under the icon (toolbar buttons with room for one)
        void setLabel (const juce::String& text) { label = text; repaint(); }
        void setShowsLabel (bool shouldShow) { if (showLabel != shouldShow) { showLabel = shouldShow; repaint(); } }
        bool hasLabel() const { return label.isNotEmpty(); }

        static juce::Font labelFont() { return juce::Font (juce::FontOptions (11.0f)); }
        int getLabelledWidth() const
        {
            juce::GlyphArrangement ga;
            ga.addLineOfText (labelFont(), label, 0.0f, 0.0f);
            return juce::jmax (44, (int) std::ceil (ga.getBoundingBox (0, -1, true).getWidth()) + 16);
        }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat();
            const bool on = getToggleState();

            if (on || over || down)
            {
                g.setColour (on ? accentDeep : raised.withAlpha (down ? 1.0f : 0.7f));
                g.fillRoundedRectangle (r, 6.0f);
            }

            const bool labelled = showLabel && label.isNotEmpty() && r.getHeight() >= 34.0f;
            const auto colour = on ? accent : (isEnabled() ? juce::Colour (0xffc3c9d0) : textDim.withAlpha (0.5f));
            auto centre = r.getCentre();
            if (labelled)
                centre.y = r.getY() + 4.0f + 9.0f;

            const auto box = juce::Rectangle<float> (18.0f, 18.0f).withCentre (centre);
            g.setColour (colour);
            g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                          juce::AffineTransform::translation (box.getX(), box.getY()));

            if (labelled)
            {
                g.setFont (labelFont());
                g.setColour (on ? accent : (isEnabled() ? textDim.brighter (0.25f) : textDim.withAlpha (0.5f)));
                g.drawText (label, r.withTop (box.getBottom() + 2.0f).withTrimmedBottom (2.0f), juce::Justification::centredTop, false);
            }
        }

    private:
        juce::Path path;
        juce::String label;
        bool showLabel = false;
    };

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel()
        {
            setColourScheme ({ 0xff1d2126, 0xff16191c, 0xff1d2126, 0xff3a4048, 0xffe6e8eb,
                               0xff4a90d9, 0xffffffff, 0xff2f5f94, 0xffe6e8eb });

            setColour (juce::ResizableWindow::backgroundColourId, background);
            setColour (juce::TextButton::buttonColourId, raised);
            setColour (juce::TextButton::buttonOnColourId, accentDeep);
            setColour (juce::TextButton::textColourOffId, text);
            setColour (juce::TextButton::textColourOnId, juce::Colours::white);
            setColour (juce::TextEditor::backgroundColourId, panel);
            setColour (juce::TextEditor::outlineColourId, border);
            setColour (juce::TextEditor::focusedOutlineColourId, accent);
            setColour (juce::ComboBox::backgroundColourId, raised);
            setColour (juce::ComboBox::outlineColourId, border);
            setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff22262b));
            setColour (juce::PopupMenu::highlightedBackgroundColourId, accentDeep);
            setColour (juce::ListBox::backgroundColourId, panel);
            setColour (juce::Label::textColourId, text);
            setColour (juce::ToggleButton::tickColourId, accent);
            setColour (juce::AlertWindow::backgroundColourId, panel);
        }

        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& backgroundColour,
                                   bool highlighted, bool down) override
        {
            auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            const float radius = juce::jmin (10.0f, r.getHeight() * 0.3f);
            const bool on = b.getToggleState();

            auto fill = on ? b.findColour (juce::TextButton::buttonOnColourId) : backgroundColour;
            if (down)             fill = fill.brighter (0.12f);
            else if (highlighted) fill = fill.brighter (0.06f);
            if (! b.isEnabled())  fill = fill.withMultipliedAlpha (0.5f);

            g.setColour (fill);
            g.fillRoundedRectangle (r, radius);

            g.setColour (on ? accent : border);
            g.drawRoundedRectangle (r, radius, 1.0f);
        }

        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
        {
            return juce::FontOptions (juce::jmin (15.0f, buttonHeight * 0.42f), juce::Font::bold);
        }
    };
}
