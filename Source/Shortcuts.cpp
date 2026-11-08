#include "Shortcuts.h"
#include "Theme.h"

namespace Shortcuts
{
    juce::String describe (const juce::KeyPress& k)
    {
        if (! k.isValid())
            return "none";

        // Readable on both platforms: "Cmd+Shift+3" rather than symbols
        juce::String s;
        const auto m = k.getModifiers();
       #if JUCE_MAC
        if (m.isCtrlDown())    s << "Ctrl+";
        if (m.isAltDown())     s << "Option+";
        if (m.isShiftDown())   s << "Shift+";
        if (m.isCommandDown()) s << "Cmd+";
       #else
        if (m.isCtrlDown())    s << "Ctrl+";
        if (m.isAltDown())     s << "Alt+";
        if (m.isShiftDown())   s << "Shift+";
       #endif
        auto plain = juce::KeyPress (k.getKeyCode()).getTextDescription();
        if (plain.length() == 1)
            plain = plain.toUpperCase();
        return s + plain;
    }

    juce::String toText (const juce::KeyPress& k)        { return k.isValid() ? k.getTextDescription() : juce::String ("none"); }
    juce::KeyPress fromText (const juce::String& t)       { return t == "none" || t.isEmpty() ? juce::KeyPress() : juce::KeyPress::createFromDescription (t); }

    bool isReserved (const juce::KeyPress& k)
    {
        if (! k.getModifiers().isCommandDown())
            return k.getKeyCode() == juce::KeyPress::tabKey;

        // Cmd+Q, W, H, M, Tab, Space, comma, and Cmd+C/V/X/Z/A for editing text
        for (auto c : { 'q', 'w', 'h', 'm', 'c', 'v', 'x', 'z', 'a', ',' })
            if (juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) k.getKeyCode()) == (juce::juce_wchar) c)
                return true;

        return k.getKeyCode() == juce::KeyPress::tabKey || k.getKeyCode() == juce::KeyPress::spaceKey;
    }

    static juce::File file() { return RackEngine::getSettingsDir().getChildFile ("shortcuts.xml"); }

    std::vector<Shortcut> loadExtras()
    {
        std::vector<Shortcut> list;
        auto xml = juce::parseXML (file());

        if (xml == nullptr)
        {
            // Default: T taps tempo
            Shortcut tap;
            tap.key = juce::KeyPress ('t');
            tap.action.type = Action::tapTempo;
            list.push_back (tap);
            return list;
        }

        for (auto* e : xml->getChildWithTagNameIterator ("SHORTCUT"))
        {
            Shortcut s;
            s.key = fromText (e->getStringAttribute ("key"));
            s.action = Action::readFrom (*e);
            if (s.key.isValid() && s.action.type != Action::none)
                list.push_back (s);
        }
        return list;
    }

    void saveExtras (const std::vector<Shortcut>& list)
    {
        juce::XmlElement xml ("SHORTCUTS");
        for (auto& s : list)
        {
            auto* e = xml.createNewChildElement ("SHORTCUT");
            e->setAttribute ("key", toText (s.key));
            s.action.writeTo (*e);
        }
        xml.writeTo (file());
    }
}

//==============================================================================
KeyCapture::KeyCapture (std::function<void (juce::KeyPress)> cb) : callback (std::move (cb))
{
    setWantsKeyboardFocus (true);
    setSize (420, 160);
}

void KeyCapture::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("Press the keys you want", getLocalBounds().removeFromTop (100), juce::Justification::centred);
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("Escape cancels", getLocalBounds().withTrimmedTop (90), juce::Justification::centredTop);
}

void KeyCapture::visibilityChanged()
{
    if (isShowing())
    {
        juce::Component::SafePointer<KeyCapture> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->grabKeyboardFocus(); });
    }
}

bool KeyCapture::keyPressed (const juce::KeyPress& key)
{
    auto* window = findParentComponentOfClass<juce::DialogWindow>();

    if (key != juce::KeyPress (juce::KeyPress::escapeKey))
    {
        auto cb = callback;
        juce::MessageManager::callAsync ([cb, key] { if (cb) cb (key); });
    }

    if (window != nullptr)
        window->exitModalState (0);
    return true;
}

void KeyCapture::launch (const juce::String& forWhat, std::function<void (juce::KeyPress)> onCaptured)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new KeyCapture (std::move (onCaptured)));
    o.dialogTitle = "Shortcut: " + forWhat;
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = false;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

//==============================================================================
ShortcutsEditor::ShortcutsEditor (Host h) : host (std::move (h))
{
    list.setRowHeight (34);
    addAndMakeVisible (list);

    help.setText ("Shortcuts work while a LiveRack window is in front (keys typed into a plugin go to the plugin). "
                  "Double-click a row to change its keys.", juce::dontSendNotification);
    help.setColour (juce::Label::textColourId, Theme::textDim);
    help.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (help);

    for (auto* b : { &addButton, &changeButton, &removeButton })
        addAndMakeVisible (b);

    addButton.onClick = [this]
    {
        juce::Component::SafePointer<ShortcutsEditor> safe (this);
        host.chooseAction ([safe] (Action a)
        {
            if (safe == nullptr || a.type == Action::none)
                return;
            safe->host.capture (safe->host.describeAction (a), [safe, a] (juce::KeyPress k)
            {
                if (safe == nullptr) return;
                safe->host.extras().push_back ({ k, a });
                safe->host.extrasChanged();
                safe->refresh();
            });
        });
    };

    changeButton.onClick = [this] { changeKey (list.getSelectedRow()); };

    removeButton.onClick = [this]
    {
        const int row = list.getSelectedRow();
        const int keys = host.numUserKeys();
        if (row < 0)
            return;

        if (row < keys)
            host.setKeyShortcut (row, {});
        else if (row - keys < (int) host.extras().size())
        {
            auto& ex = host.extras();
            ex.erase (ex.begin() + (row - keys));
            host.extrasChanged();
        }
        refresh();
    };

    setSize (620, 560);
}

ShortcutsEditor::~ShortcutsEditor()
{
    list.setModel (nullptr);
}

void ShortcutsEditor::refresh()
{
    list.updateContent();
    list.repaint();
}

int ShortcutsEditor::getNumRows()
{
    return host.numUserKeys() + (int) host.extras().size();
}

void ShortcutsEditor::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (selected)
        g.fillAll (Theme::accentDeep);

    const int keys = host.numUserKeys();
    juce::KeyPress key;
    juce::String what;

    if (row < keys)
    {
        key = host.getKeyShortcut (row);
        what = "User Key " + juce::String (row + 1) + ": " + host.describeKey (row);
    }
    else if (row - keys < (int) host.extras().size())
    {
        const auto& s = host.extras()[(size_t) (row - keys)];
        key = s.key;
        what = host.describeAction (s.action);
    }

    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (10, 4);
    auto keyBox = area.removeFromLeft (130).toFloat();
    g.setColour (key.isValid() ? Theme::raised : Theme::panel);
    g.fillRoundedRectangle (keyBox, 6.0f);
    g.setColour (key.isValid() ? Theme::text : Theme::textDim);
    g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
    g.drawText (Shortcuts::describe (key), keyBox.toNearestInt(), juce::Justification::centred);

    area.removeFromLeft (14);
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (what, area, juce::Justification::centredLeft, true);
}

void ShortcutsEditor::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    changeKey (row);
}

void ShortcutsEditor::changeKey (int row)
{
    const int keys = host.numUserKeys();
    if (row < 0 || row >= getNumRows())
        return;

    juce::Component::SafePointer<ShortcutsEditor> safe (this);
    const auto label = row < keys ? "User Key " + juce::String (row + 1)
                                  : host.describeAction (host.extras()[(size_t) (row - keys)].action);

    host.capture (label, [safe, row, keys] (juce::KeyPress k)
    {
        if (safe == nullptr) return;
        if (row < keys)
            safe->host.setKeyShortcut (row, k);
        else if (row - keys < (int) safe->host.extras().size())
        {
            safe->host.extras()[(size_t) (row - keys)].key = k;
            safe->host.extrasChanged();
        }
        safe->refresh();
    });
}

void ShortcutsEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
}

void ShortcutsEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    help.setBounds (area.removeFromTop (40));
    area.removeFromTop (8);
    auto buttons = area.removeFromBottom (44);
    addButton.setBounds (buttons.removeFromLeft (150));
    buttons.removeFromLeft (8);
    changeButton.setBounds (buttons.removeFromLeft (150));
    removeButton.setBounds (buttons.removeFromRight (110));
    area.removeFromBottom (10);
    list.setBounds (area);
}
