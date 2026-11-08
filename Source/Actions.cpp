#include "Actions.h"

juce::String Action::describe (const RackEngine& engine) const
{
    auto rackName = [&engine] (int r) -> juce::String
    {
        if (r < 0) return "selected rack";
        if (r < engine.getNumRacks()) return engine.getRackConfig (r).name;
        return "rack " + juce::String (r + 1);
    };

    auto pluginName = [&engine] (int id) -> juce::String
    {
        if (id <= 0) return "selected plugin";
        int r, s;
        if (engine.findSlot (id, r, s))
            if (auto* p = engine.getPlugin (r, s))
                return engine.getRackConfig (r).name + " " + p->getName();
        return "(removed plugin)";
    };

    switch (type)
    {
        case rackBypass:    return "Bypass " + rackName (rack);
        case pluginBypass:  return "Bypass " + pluginName (slotId);
        case selectRack:    return "Select " + rackName (rack);
        case openPlugin:    return "Open " + pluginName (slotId);
        case hotPlugin:     return "Hot Plugin " + juce::String (number);
        case recallScene:   return "Scene " + juce::String (number);
        case nextScene:     return "Next Scene";
        case prevScene:     return "Previous Scene";
        case tapTempo:      return "Tap Tempo";
        case twoWindows:    return "Two Windows";
        case storeScene:    return "Store Scene";
        case showPage:
            if (number <= 0) return "Page: All";
            if (number <= (int) engine.getPages().size()) return "Page: " + engine.getPages()[(size_t) number - 1].name;
            return "Page " + juce::String (number);
        case none: case numTypes: default: break;
    }

    return "Not assigned";
}

juce::String Action::category() const
{
    switch (type)
    {
        case rackBypass:    return "rack bypass";
        case pluginBypass:  return "plugin bypass";
        case selectRack:    return "select rack";
        case openPlugin:    return "open plugin";
        case hotPlugin:     return "hot plugin";
        case recallScene:   return "recall";
        case nextScene: case prevScene: return "recall";
        case tapTempo:      return "BPM";
        case twoWindows:    return "view";
        case storeScene:    return "store";
        case showPage:      return "page";
        case none: case numTypes: default: break;
    }
    return "right-click to assign";
}

void Action::writeTo (juce::XmlElement& x) const
{
    x.setAttribute ("action", (int) type);
    x.setAttribute ("rack", rack);
    x.setAttribute ("slotId", slotId);
    x.setAttribute ("number", number);
}

Action Action::readFrom (const juce::XmlElement& x)
{
    Action a;
    a.type = (Type) juce::jlimit (0, (int) numTypes - 1, x.getIntAttribute ("action", 0));
    a.rack = x.getIntAttribute ("rack", -1);
    a.slotId = x.getIntAttribute ("slotId", 0);
    a.number = x.getIntAttribute ("number", 0);
    return a;
}

void Action::showChooser (const RackEngine& engine, const Action& current, int selectedRack,
                          std::function<void (Action)> onChosen)
{
    std::vector<Action> items;
    juce::PopupMenu m;

    auto add = [&] (juce::PopupMenu& menu, const Action& a, const juce::String& text)
    {
        items.push_back (a);
        menu.addItem ((int) items.size(), text, true, a == current);
    };

    auto make = [] (Type t, int rackIndex = -1, int id = 0, int n = 0)
    {
        Action a;
        a.type = t; a.rack = rackIndex; a.slotId = id; a.number = n;
        return a;
    };

    juce::PopupMenu bypassRack, selectRackMenu, bypassPlugin, openPluginMenu, hot, scenes;

    add (bypassRack, make (rackBypass, -1), "Selected rack (follows selection)");

    for (int r = 0; r < engine.getNumRacks(); ++r)
    {
        const auto name = engine.getRackConfig (r).name;
        add (bypassRack, make (rackBypass, r), name);
        add (selectRackMenu, make (selectRack, r), name);

        juce::PopupMenu rackPlugins, rackOpen;
        for (int s = 0; s < engine.getNumSlots (r); ++s)
            if (auto* p = engine.getPlugin (r, s))
            {
                add (rackPlugins, make (pluginBypass, -1, engine.getSlotId (r, s)), p->getName());
                add (rackOpen, make (openPlugin, -1, engine.getSlotId (r, s)), p->getName());
            }

        if (rackPlugins.getNumItems() > 0)
        {
            bypassPlugin.addSubMenu (name, rackPlugins);
            openPluginMenu.addSubMenu (name, rackOpen);
        }
    }

    add (bypassPlugin, make (pluginBypass, -1, 0), "Selected plugin (follows selection)");

    for (int i = 1; i <= RackEngine::maxHotPlugins; ++i)
        add (hot, make (hotPlugin, -1, 0, i), "Hot Plugin " + juce::String (i));

    for (int i = 0; i <= 30; ++i)
        add (scenes, make (recallScene, -1, 0, i), "Scene " + juce::String (i));

    m.addSectionHeader ("Assign");
    m.addSubMenu ("Bypass rack", bypassRack);
    m.addSubMenu ("Bypass plugin", bypassPlugin);
    m.addSubMenu ("Select rack", selectRackMenu);
    m.addSubMenu ("Open plugin", openPluginMenu);
    m.addSubMenu ("Hot Plugin", hot);

    juce::PopupMenu pagesMenu;
    add (pagesMenu, make (showPage, -1, 0, 0), "All");
    for (int i = 0; i < (int) engine.getPages().size(); ++i)
        add (pagesMenu, make (showPage, -1, 0, i + 1), engine.getPages()[(size_t) i].name);
    m.addSubMenu ("Show page", pagesMenu);
    m.addSeparator();
    m.addSubMenu ("Recall scene", scenes);
    add (m, make (nextScene), "Next scene");
    add (m, make (prevScene), "Previous scene");
    add (m, make (storeScene), "Store scene (current number)");
    m.addSeparator();
    add (m, make (tapTempo), "Tap tempo");
    add (m, make (twoWindows), "Two windows on/off");
    m.addSeparator();
    add (m, make (none), "Clear");

    juce::ignoreUnused (selectedRack);

    m.showMenuAsync (juce::PopupMenu::Options(), [items, onChosen] (int result)
    {
        if (result > 0 && result <= (int) items.size() && onChosen != nullptr)
            onChosen (items[(size_t) result - 1]);
    });
}
