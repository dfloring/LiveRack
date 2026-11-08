#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Knows about the scanned plugins as "families": one entry per plugin, with
// its mono/stereo/surround versions and formats folded together (Waves lists
// most plugins several times over). Also keeps the user's favourites,
// recently used plugins, category corrections and GR settings.
class PluginLibrary
{
public:
    enum class Category { dynamics, eq, reverb, delay, modulation, channelStrip, utility, other };
    static constexpr int numCategories = 8;
    static juce::String categoryName (Category);

    struct Family
    {
        juce::String key;          // manufacturer|name, used for favourites etc.
        juce::String name, manufacturer;
        Category category = Category::other;
        juce::Array<juce::PluginDescription> versions;
    };

    explicit PluginLibrary (juce::File settingsFile);

    // Rebuild the families from the scanned plugin list (effects only)
    void rebuild (const juce::Array<juce::PluginDescription>& types);
    const std::vector<Family>& getFamilies() const { return families; }
    const Family* findFamily (const juce::String& key) const;

    // The version to load for a mono or stereo rack
    static juce::PluginDescription bestVersionFor (const Family&, bool stereoRack);

    // Mono, stereo or mono-in/stereo-out (for mono-to-stereo racks)
    enum class Want { mono, stereo, monoToStereo };
    static juce::PluginDescription bestVersionFor (const Family&, Want);
    static bool hasVersion (const Family&, Want);
    // Effects that usually turn a mono source stereo (reverbs, delays, modulation)
    static bool suitsStereoPoint (Category c) { return c == Category::reverb || c == Category::delay || c == Category::modulation; }

    static juce::String familyKey (const juce::PluginDescription&);
    static juce::String baseName (const juce::String& pluginName);
    static juce::String versionLabel (const juce::PluginDescription&); // "Mono", "Stereo", "5.1"... or ""

    // Favourites and recents
    bool isFavourite (const juce::String& key) const { return favourites.contains (key); }
    void setFavourite (const juce::String& key, bool);
    const juce::StringArray& getFavourites() const { return favourites; }
    void noteUsed (const juce::String& key);
    const juce::StringArray& getRecents() const { return recents; }

    // Categories
    Category categoryFor (const juce::PluginDescription&) const;
    void setCategoryOverride (const juce::String& key, Category);

    // Gain reduction: a parameter the user picked, and whether to show an
    // estimate for plugins that don't report GR
    int  getGainReductionParameterOverride (const juce::String& key) const; // -1 = none
    void setGainReductionParameterOverride (const juce::String& key, int parameterIndex);
    bool getShowEstimatedGR (const juce::String& key, Category) const;
    void setShowEstimatedGR (const juce::String& key, bool);

private:
    static Category guessCategory (const juce::PluginDescription&);
    void save() const;
    void load();

    juce::File file;
    std::vector<Family> families;
    juce::StringArray favourites, recents;
    std::map<juce::String, int> categoryOverrides, grParameterOverrides, estimateOverrides;
};
