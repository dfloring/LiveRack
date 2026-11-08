#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// LiveRack's colours and the look of buttons: rounded pills, lit when on,
// sized for touch screens
namespace Theme
{
    const juce::Colour background { 0xff16191c };
    const juce::Colour panel      { 0xff1d2126 };
    const juce::Colour raised     { 0xff262a30 };
    const juce::Colour border     { 0xff2f353c };
    const juce::Colour text       { 0xffe6e8eb };
    const juce::Colour textDim    { 0xff9aa3ad };
    const juce::Colour accent     { 0xff4a90d9 };
    const juce::Colour accentDeep { 0xff24364c };
    const juce::Colour amber      { 0xffe0a030 };
    const juce::Colour green      { 0xff3fb950 };
    const juce::Colour red        { 0xffd95454 };

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
