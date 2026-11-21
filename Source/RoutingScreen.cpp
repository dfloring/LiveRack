#include "RoutingScreen.h"
#include "Theme.h"

RoutingScreen::RoutingScreen (LiveConnection& e) : matrix (e), links (e)
{
    matrix.onSelectRack = [this] (int r) { if (onSelectRack) onSelectRack (r); };

    for (auto* b : { &patchTab, &linksTab })
        addAndMakeVisible (b);
    patchTab.onClick = [this] { showTab (0); };
    linksTab.onClick = [this] { showTab (1); };

    addChildComponent (matrix);
    addChildComponent (links);
    setSize (1300, 780);
    showTab (lastTab);
}

RoutingScreen::~RoutingScreen() = default;

void RoutingScreen::showTab (int t)
{
    tab = lastTab = t;
    patchTab.setToggleState (t == 0, juce::dontSendNotification);
    linksTab.setToggleState (t == 1, juce::dontSendNotification);
    matrix.setVisible (t == 0);
    links.setVisible (t == 1);
    refresh();
    repaint();
}

void RoutingScreen::refresh()
{
    if (tab == 0) matrix.refresh();
    else          links.refresh();
}

void RoutingScreen::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
    // The active tab is underlined
    auto& b = tab == 0 ? patchTab : linksTab;
    g.setColour (Theme::accent);
    g.fillRect (b.getX(), b.getBottom() + 2, b.getWidth(), 3);
}

void RoutingScreen::resized()
{
    auto area = getLocalBounds();
    auto bar = area.removeFromTop (48).reduced (16, 2);
    patchTab.setBounds (bar.removeFromLeft (130));
    bar.removeFromLeft (4);
    linksTab.setBounds (bar.removeFromLeft (150));
    area.removeFromTop (4);
    matrix.setBounds (area);
    links.setBounds (area);
}
