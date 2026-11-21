#pragma once
#include "RoutingMatrix.h"
#include "ConsoleLinksGrid.h"

// The Routing screen: tabs across the top, each tab its own grid.
//   Patch          the routing matrix (rack channels to audio driver channels)
//   Console links  which console channel selects which rack
// (A Latency tab joins in step 2 of #53.) Native and Client host this the same way.
class RoutingScreen : public juce::Component
{
public:
    explicit RoutingScreen (LiveConnection&);
    ~RoutingScreen() override;

    std::function<void (int rack)> onSelectRack;

    void setSelectedRack (int rack)  { matrix.setSelectedRack (rack); }
    void refresh();
    void showTab (int);                          // 0 = Patch, 1 = Console links
    ConsoleLinksGrid& getConsoleLinks() { return links; }

    void paint (juce::Graphics&) override;
    void resized() override;

#if LIVERACK_PREVIEW_HOOKS
    friend struct PreviewHooks;
   #endif
private:
    static inline int lastTab = 0;   // remembered while the app runs

    RoutingMatrix matrix;
    ConsoleLinksGrid links;
    int tab = 0;
    juce::TextButton patchTab { "Patch" }, linksTab { "Console links" };
};
