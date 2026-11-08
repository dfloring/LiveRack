#include "PluginPicker.h"

namespace
{
    const juce::Colour accent    { 0xff4a90d9 };
    const juce::Colour starOn    { 0xffe8c13a };
    const juce::Colour panelBack { 0xff1f2328 };

    juce::Colour categoryColour (int c)
    {
        static const juce::uint32 colours[] = { 0xffe0a030, 0xff4a90d9, 0xff3aa9a0, 0xffb07cd8, 0xffd96ba8,
                                                0xff52b788, 0xffd9c94a, 0xff8a939d, 0xff5a626b, 0xff7f8c99, 0xff6b7580 };
        return c < 0 ? juce::Colour (0xffe6e8eb) : juce::Colour (colours[(size_t) c % (sizeof (colours) / sizeof (colours[0]))]);
    }

    // A big category button with a colour bar
    class CategoryTile : public juce::TextButton
    {
    public:
        CategoryTile (const juce::String& name, int c) : juce::TextButton (name), category (c) {}

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            const bool on = getToggleState();
            const auto col = categoryColour (category);

            g.setColour (on ? col.withAlpha (0.18f) : juce::Colour (0xff1d2126).brighter (over ? 0.06f : 0.0f).brighter (down ? 0.1f : 0.0f));
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (on ? col : juce::Colour (0xff2f353c));
            g.drawRoundedRectangle (r, 10.0f, on ? 2.0f : 1.0f);

            g.setColour (col);
            g.fillRoundedRectangle (juce::Rectangle<float> (26.0f, 5.0f).withCentre ({ r.getCentreX(), r.getY() + r.getHeight() * 0.34f }), 2.5f);

            g.setColour (juce::Colours::white.withAlpha (0.92f));
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawFittedText (getButtonText(), r.withTrimmedTop (r.getHeight() * 0.45f).toNearestInt().reduced (4, 0),
                              juce::Justification::centredTop, 1, 0.7f);
        }

    private:
        const int category;
    };

    juce::String starText (bool on)
    {
        return juce::String (juce::CharPointer_UTF8 (on ? "\xe2\x98\x85" : "\xe2\x98\x86")); // filled / outline star
    }
}

//==============================================================================
class PluginPicker::ResultsModel : public juce::ListBoxModel
{
public:
    explicit ResultsModel (PluginPicker& p) : picker (p) {}

    int getNumRows() override { return (int) picker.results.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, (int) picker.results.size()))
            return;

        const auto& e = picker.results[(size_t) row];
        const auto& f = *e.family;

        if (selected)
            g.fillAll (accent.withAlpha (0.45f));

        auto area = juce::Rectangle<int> (0, 0, width, height).reduced (8, 0);

        const bool fav = picker.library.isFavourite (f.key);
        g.setColour (fav ? starOn : juce::Colours::white.withAlpha (0.3f));
        g.setFont (juce::FontOptions (16.0f));
        g.drawText (starText (fav), area.removeFromLeft (24), juce::Justification::centredLeft);

        // Category tag on the right
        g.setFont (juce::FontOptions (11.5f));
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.drawText (PluginLibrary::categoryName (f.category), area.removeFromRight (90), juce::Justification::centredRight);

        // Manufacturer
        g.drawText (f.manufacturer, area.removeFromRight (140), juce::Justification::centredRight, true);

        // Name (+ specific version when showing every version)
        juce::String name = f.name;
        if (e.version >= 0)
        {
            const auto& d = f.versions.getReference (e.version);
            const auto label = PluginLibrary::versionLabel (d);
            name << "   " << (label.isNotEmpty() ? label + " " : juce::String()) << "[" << d.pluginFormatName << "]";
        }

        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (14.5f));
        g.drawText (name, area, juce::Justification::centredLeft, true);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (! juce::isPositiveAndBelow (row, (int) picker.results.size()))
            return;

        const auto entry = picker.results[(size_t) row];

        if (e.mods.isPopupMenu())
        {
            picker.showContextMenu (entry);
        }
        else if (e.x < 32) // the star
        {
            picker.library.setFavourite (entry.family->key, ! picker.library.isFavourite (entry.family->key));
            picker.sideList.updateContent();
            picker.sideList.repaint();
            picker.resultsList.repaint();
        }
        else
        {
            picker.choose (entry);
        }
    }

    void returnKeyPressed (int row) override
    {
        if (juce::isPositiveAndBelow (row, (int) picker.results.size()))
            picker.choose (picker.results[(size_t) row]);
    }

    // Drag a plugin straight onto a rack strip
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override
    {
        if (rows.isEmpty() || ! juce::isPositiveAndBelow (rows[0], (int) picker.results.size()))
            return {};

        const auto& e = picker.results[(size_t) rows[0]];
        if (e.version >= 0)
            return "plugin:id:" + e.family->versions[e.version].createIdentifierString();
        return "plugin:family:" + e.family->key;
    }

private:
    PluginPicker& picker;
};

//==============================================================================
// Favourites and recents, with section headers
class PluginPicker::SideModel : public juce::ListBoxModel
{
public:
    explicit SideModel (PluginPicker& p) : picker (p) {}

    struct Row { juce::String header; const PluginLibrary::Family* family = nullptr; };

    std::vector<Row> rows() const
    {
        std::vector<Row> r;
        r.push_back ({ "FAVOURITES", nullptr });

        bool anyFav = false;
        for (auto& key : picker.library.getFavourites())
            if (auto* f = picker.library.findFamily (key))
            {
                r.push_back ({ {}, f });
                anyFav = true;
            }
        if (! anyFav)
            r.push_back ({ "Click a star to add", nullptr });

        r.push_back ({ "RECENT", nullptr });
        for (auto& key : picker.library.getRecents())
            if (auto* f = picker.library.findFamily (key))
                r.push_back ({ {}, f });

        return r;
    }

    int getNumRows() override { return (int) rows().size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool) override
    {
        const auto all = rows();
        if (! juce::isPositiveAndBelow (row, (int) all.size()))
            return;

        const auto& r = all[(size_t) row];
        auto area = juce::Rectangle<int> (0, 0, width, height).reduced (10, 0);

        if (r.family == nullptr)
        {
            const bool isHeader = r.header.toUpperCase() == r.header;
            g.setColour (juce::Colours::white.withAlpha (isHeader ? 0.5f : 0.3f));
            g.setFont (juce::FontOptions (isHeader ? 11.0f : 12.0f, isHeader ? juce::Font::bold : juce::Font::italic));
            g.drawText (r.header, area, juce::Justification::bottomLeft);
            return;
        }

        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (r.family->name, area, juce::Justification::centredLeft, true);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        const auto all = rows();
        if (! juce::isPositiveAndBelow (row, (int) all.size()) || all[(size_t) row].family == nullptr)
            return;

        Entry entry { all[(size_t) row].family, -1 };

        if (e.mods.isPopupMenu()) picker.showContextMenu (entry);
        else                      picker.choose (entry);
    }

private:
    PluginPicker& picker;
};

//==============================================================================
PluginPicker::PluginPicker (PluginLibrary& lib, WantFn want, const juce::String& rackText,
                            std::function<void (const juce::PluginDescription&)> chosen)
    : library (lib), wantFor (std::move (want)), rackDescription (rackText), onChosen (std::move (chosen)),
      resultsModel (std::make_unique<ResultsModel> (*this)),
      sideModel (std::make_unique<SideModel> (*this))
{
    search.setTextToShowWhenEmpty ("Search plugins...", juce::Colours::grey);
    search.setFont (juce::FontOptions (16.0f));
    search.setJustification (juce::Justification::centredLeft);
    search.setIndents (12, 0);
    search.onTextChange = [this] { refilter(); };
    search.addKeyListener (this);
    addAndMakeVisible (search);

    auto addCategoryButton = [this] (const juce::String& name, int category)
    {
        auto* b = categoryButtons.add (new CategoryTile (name, category));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1);
        b->setToggleState (category == categoryFilter, juce::dontSendNotification);
        b->onClick = [this, category] { categoryFilter = category; refilter(); };
        addAndMakeVisible (b);
    };

    addCategoryButton ("All", -1);
    for (int c = 0; c < PluginLibrary::numCategories; ++c)
        addCategoryButton (PluginLibrary::categoryName ((PluginLibrary::Category) c), c);

    allVersions.onClick = [this] { refilter(); };
    addAndMakeVisible (allVersions);

    countLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.5f));
    countLabel.setFont (juce::FontOptions (12.0f));
    countLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (countLabel);

    resultsList.setModel (resultsModel.get());
    resultsList.setRowHeight (34);
    resultsList.setColour (juce::ListBox::backgroundColourId, panelBack);
    addAndMakeVisible (resultsList);

    sideList.setModel (sideModel.get());
    sideList.setRowHeight (32);

    dragHint.setText ("Tip: drag a plugin straight onto any rack strip", juce::dontSendNotification);
    dragHint.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.5f));
    dragHint.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (dragHint);
    sideList.setColour (juce::ListBox::backgroundColourId, panelBack);
    addAndMakeVisible (sideList);

    refilter();
    setSize (1000, 660);
}

PluginPicker::~PluginPicker()
{
    search.removeKeyListener (this);
    resultsList.setModel (nullptr);
    sideList.setModel (nullptr);
}

void PluginPicker::visibilityChanged()
{
    if (isShowing())
    {
        juce::Component::SafePointer<juce::TextEditor> safe (&search);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->grabKeyboardFocus(); });
    }
}

void PluginPicker::refilter()
{
    results.clear();

    juce::StringArray terms;
    terms.addTokens (search.getText().toLowerCase(), " ", "");
    terms.removeEmptyStrings();

    for (auto& f : library.getFamilies())
    {
        if (categoryFilter >= 0 && (int) f.category != categoryFilter)
            continue;

        const auto haystack = (f.name + " " + f.manufacturer).toLowerCase();
        bool matches = true;
        for (auto& t : terms)
            if (! haystack.contains (t))
                matches = false;

        if (! matches)
            continue;

        if (allVersions.getToggleState())
            for (int v = 0; v < f.versions.size(); ++v)
                results.push_back ({ &f, v });
        else
            results.push_back ({ &f, -1 });
    }

    // Name matches that start with the search come first
    if (! terms.isEmpty())
        std::stable_sort (results.begin(), results.end(), [&terms] (const Entry& a, const Entry& b)
        {
            const bool aStarts = a.family->name.toLowerCase().startsWith (terms[0]);
            const bool bStarts = b.family->name.toLowerCase().startsWith (terms[0]);
            return aStarts && ! bStarts;
        });

    countLabel.setText (juce::String ((int) results.size()) + (results.size() == 1 ? " plugin" : " plugins")
                          + "  (" + rackDescription + ")",
                        juce::dontSendNotification);

    resultsList.updateContent();
    resultsList.selectRow (0);
    resultsList.repaint();
}

void PluginPicker::choose (const Entry& e)
{
    if (e.family == nullptr)
        return;

    const auto desc = e.version >= 0 ? e.family->versions[e.version]
                                     : PluginLibrary::bestVersionFor (*e.family, wantFor (*e.family));
    library.noteUsed (e.family->key);

    // Copy what we need: closing may delete this picker
    auto callback = onChosen;
    auto closer = onClose;

    if (closer != nullptr)
        closer();

    if (callback != nullptr)
        callback (desc);
}

void PluginPicker::showContextMenu (const Entry& e)
{
    juce::PopupMenu m;
    m.addSectionHeader (e.family->name);
    m.addItem (1, library.isFavourite (e.family->key) ? "Remove from favourites" : "Add to favourites");

    juce::PopupMenu categories;
    for (int c = 0; c < PluginLibrary::numCategories; ++c)
        categories.addItem (100 + c, PluginLibrary::categoryName ((PluginLibrary::Category) c), true, (int) e.family->category == c);
    m.addSubMenu ("Category", categories);

    const auto key = e.family->key;
    juce::Component::SafePointer<PluginPicker> safe (this);

    m.showMenuAsync (juce::PopupMenu::Options(), [safe, key] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        if (result == 1)
            safe->library.setFavourite (key, ! safe->library.isFavourite (key));
        else if (result >= 100)
            safe->library.setCategoryOverride (key, (PluginLibrary::Category) (result - 100));

        safe->refilter();
        safe->sideList.updateContent();
        safe->sideList.repaint();
    });
}

bool PluginPicker::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    const int row = resultsList.getSelectedRow();

    if (key == juce::KeyPress::downKey)
    {
        resultsList.selectRow (juce::jmin ((int) results.size() - 1, row + 1));
        return true;
    }

    if (key == juce::KeyPress::upKey)
    {
        resultsList.selectRow (juce::jmax (0, row - 1));
        return true;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        if (onClose != nullptr)
            onClose();
        return true;
    }

    if (key == juce::KeyPress::returnKey)
    {
        if (juce::isPositiveAndBelow (row, (int) results.size()))
            choose (results[(size_t) row]);
        return true;
    }

    return false;
}

void PluginPicker::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff16191c));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 14.0f);
    g.setColour (juce::Colour (0xff2f353c));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 14.0f, 1.0f);
}

void PluginPicker::resized()
{
    auto area = getLocalBounds().reduced (16);

    search.setBounds (area.removeFromTop (44));
    area.removeFromTop (12);

    auto cats = area.removeFromTop (64);
    const int w = cats.getWidth() / categoryButtons.size();
    for (auto* b : categoryButtons)
        b->setBounds (cats.removeFromLeft (w).reduced (3, 0));

    area.removeFromTop (12);
    auto footer = area.removeFromBottom (28);
    allVersions.setBounds (footer.removeFromLeft (200));
    dragHint.setBounds (footer.removeFromLeft (320));
    countLabel.setBounds (footer);
    area.removeFromBottom (6);

    sideList.setBounds (area.removeFromLeft (220));
    area.removeFromLeft (8);
    resultsList.setBounds (area);
}
