#include "PluginLibrary.h"
#include <map>

namespace
{
    // Version suffixes that plugin makers (Waves especially) append to names
    const char* versionSuffixes[] = {
        "mono/stereo", "mono to stereo", "mono-stereo", "m/s", "m>s", "mono", "stereo",
        "5.0", "5.1", "6.0", "6.1", "7.0", "7.1", "7.0.2", "7.1.2", "7.0.4", "7.1.4", "9.1.6", "lcr", "lcrs", "quad"
    };

    bool isSurroundLabel (const juce::String& label)
    {
        return label.isNotEmpty() && ! label.equalsIgnoreCase ("mono") && ! label.equalsIgnoreCase ("stereo")
               && ! label.equalsIgnoreCase ("mono/stereo") && ! label.equalsIgnoreCase ("mono to stereo")
               && ! label.equalsIgnoreCase ("mono-stereo") && ! label.equalsIgnoreCase ("m/s") && ! label.equalsIgnoreCase ("m>s");
    }

    bool containsAny (const juce::String& text, std::initializer_list<const char*> words)
    {
        for (auto* w : words)
            if (text.contains (w))
                return true;
        return false;
    }

    bool hasWord (const juce::String& text, std::initializer_list<const char*> words)
    {
        juce::StringArray tokens;
        tokens.addTokens (text, " -_()/.", "");
        for (auto* w : words)
            if (tokens.contains (w))
                return true;
        return false;
    }
}

juce::String PluginLibrary::categoryName (Category c)
{
    switch (c)
    {
        case Category::dynamics:     return "Dynamics";
        case Category::eq:           return "EQ";
        case Category::reverb:       return "Reverb";
        case Category::delay:        return "Delay";
        case Category::modulation:   return "Modulation";
        case Category::channelStrip: return "Channel Strip";
        case Category::utility:      return "Utility";
        case Category::other:        break;
    }
    return "Other";
}

PluginLibrary::PluginLibrary (juce::File settingsFile) : file (std::move (settingsFile))
{
    load();
}

//==============================================================================
juce::String PluginLibrary::versionLabel (const juce::PluginDescription& d)
{
    const auto lower = d.name.toLowerCase().trim();

    for (auto* suffix : versionSuffixes)
    {
        const juce::String sfx (suffix);
        if (lower.endsWith (" " + sfx) || lower.endsWith ("(" + sfx + ")") || lower.endsWith ("-" + sfx))
            return sfx == "mono" ? "Mono" : sfx == "stereo" ? "Stereo" : sfx.toUpperCase().replace ("MONO/STEREO", "Mono/Stereo");
    }

    return {};
}

juce::String PluginLibrary::baseName (const juce::String& pluginName)
{
    auto name = pluginName.trim();
    const auto lower = name.toLowerCase();

    for (auto* suffix : versionSuffixes)
    {
        const juce::String sfx (suffix);
        for (auto pattern : { " " + sfx, "(" + sfx + ")", " (" + sfx + ")", "-" + sfx })
            if (lower.endsWith (pattern))
                return name.dropLastCharacters (pattern.length()).trim();
    }

    return name;
}

juce::String PluginLibrary::familyKey (const juce::PluginDescription& d)
{
    return d.manufacturerName.trim().toLowerCase() + "|" + baseName (d.name).toLowerCase();
}

//==============================================================================
void PluginLibrary::rebuild (const juce::Array<juce::PluginDescription>& types)
{
    std::map<juce::String, Family> byKey;

    for (auto& d : types)
    {
        if (d.isInstrument)
            continue;

        auto& f = byKey[familyKey (d)];
        if (f.key.isEmpty())
        {
            f.key = familyKey (d);
            f.name = baseName (d.name);
            f.manufacturer = d.manufacturerName;
        }
        f.versions.add (d);
    }

    families.clear();
    for (auto& [key, f] : byKey)
    {
        f.category = categoryFor (f.versions.getReference (0));
        families.push_back (std::move (f));
    }

    std::sort (families.begin(), families.end(), [] (const Family& a, const Family& b)
               { return a.name.compareNatural (b.name) < 0; });
}

const PluginLibrary::Family* PluginLibrary::findFamily (const juce::String& key) const
{
    for (auto& f : families)
        if (f.key == key)
            return &f;
    return nullptr;
}

juce::PluginDescription PluginLibrary::bestVersionFor (const Family& f, Want want)
{
    if (want != Want::monoToStereo)
        return bestVersionFor (f, want == Want::stereo);

    const juce::PluginDescription* best = nullptr;
    int bestScore = -1000;

    for (auto& d : f.versions)
    {
        const auto label = versionLabel (d).toLowerCase();
        int score = 0;
        if (label == "mono/stereo")     score += 100;
        else if (label.isEmpty())       score += 80;   // flexible: LiveRack asks it for mono in, stereo out
        else if (label == "stereo")     score += 40;
        else if (label == "mono")       score += 10;
        if (isSurroundLabel (label))    score -= 200;
        if (d.pluginFormatName == "VST3") score += 5;

        if (score > bestScore) { bestScore = score; best = &d; }
    }

    return best != nullptr ? *best : juce::PluginDescription {};
}

bool PluginLibrary::hasVersion (const Family& f, Want want)
{
    const juce::String wanted = want == Want::mono ? "mono" : want == Want::stereo ? "stereo" : "mono/stereo";
    for (auto& d : f.versions)
        if (versionLabel (d).toLowerCase() == wanted)
            return true;
    return false;
}

juce::PluginDescription PluginLibrary::bestVersionFor (const Family& f, bool stereoRack)
{
    // Score each version: right channel version first, VST3 preferred over AU
    const juce::PluginDescription* best = nullptr;
    int bestScore = -1000;

    for (auto& d : f.versions)
    {
        const auto label = versionLabel (d).toLowerCase();
        int score = 0;

        if (stereoRack)
        {
            if (label == "stereo")                         score += 100;
            else if (label.isEmpty())                      score += 80;
            else if (label == "mono/stereo")               score += 40;
            else if (label == "mono")                      score += 10;
        }
        else
        {
            if (label == "mono")                           score += 100;
            else if (label.isEmpty())                      score += 80;
            else if (label == "mono/stereo")               score += 60;
            else if (label == "stereo")                    score += 10;
        }

        if (isSurroundLabel (label))                       score -= 200;
        if (d.pluginFormatName == "VST3")                  score += 5;

        if (score > bestScore)
        {
            bestScore = score;
            best = &d;
        }
    }

    return best != nullptr ? *best : juce::PluginDescription {};
}

//==============================================================================
PluginLibrary::Category PluginLibrary::guessCategory (const juce::PluginDescription& d)
{
    const auto name = baseName (d.name).toLowerCase();
    const auto vstCategory = d.category.toLowerCase(); // VST3 sub-categories, e.g. "Fx|Dynamics"

    // Names first: they're more specific than generic plugin categories
    if (containsAny (name, { "channel strip", "e-channel", "g-channel", "omni channel", "audiotrack", "mixhub",
                             "renaissance channel", "channel" }))
        return Category::channelStrip;

    if (containsAny (name, { "verb", "reverb", "chamber", "plate", "abbey road plates", "ir-1", "ir1", "ir-l", "space" }))
        return Category::reverb;

    if (containsAny (name, { "delay", "echo", "supertap", "h-delay", "tape delay" }))
        return Category::delay;

    if (containsAny (name, { "chorus", "flanger", "phaser", "doubler", "mondomod", "enigma", "tremolo", "vibrato",
                             "rotary", "tune", "pitch", "soundshifter", "morphoder", "ultrapitch" }))
        return Category::modulation;

    if (containsAny (name, { "comp", "limit", "gate", "expand", "de-ess", "deess", "rider", "maxx", "transient",
                             "trans-x", "smack", "muteomatic", "leveler", "dynamic", "ducker", "sibilance",
                             "cla-2a", "cla-3a", "cla-76", "r-vox", "r-comp", "1176", "la-2a", "la-3a",
                             "puigchild", "v-comp", "h-comp", "dbx", "2500" })
        || hasWord (name, { "c1", "c4", "c6", "l1", "l2", "l3", "mv2", "rvox", "rcomp", "ns1" }))
        return Category::dynamics;

    if (containsAny (name, { "eq", "equalizer", "pultec", "puigtec", "q10", "q-clone", "filter" })
        || hasWord (name, { "f6", "h-eq", "api-550", "api-560", "550", "560", "pq4", "v-eq" }))
        return Category::eq;

    if (containsAny (name, { "meter", "analy", "paz", "wlm", "vu ", "scope", "trim", "gain", "utility", "imager",
                             "inphase", "phase", "stereo", "tuner", "x-hum", "x-click", "clarity", "noise" })
        || hasWord (name, { "s1", "vu", "pan" }))
        return Category::utility;

    // Then the plugin's own category, if it has one
    if (containsAny (vstCategory, { "dynamics" }))            return Category::dynamics;
    if (containsAny (vstCategory, { "eq", "filter" }))         return Category::eq;
    if (containsAny (vstCategory, { "reverb" }))               return Category::reverb;
    if (containsAny (vstCategory, { "delay" }))                return Category::delay;
    if (containsAny (vstCategory, { "modulation", "pitch" }))  return Category::modulation;
    if (containsAny (vstCategory, { "analyzer", "tools", "restoration", "mastering" })) return Category::utility;

    return Category::other;
}

PluginLibrary::Category PluginLibrary::categoryFor (const juce::PluginDescription& d) const
{
    const auto it = categoryOverrides.find (familyKey (d));
    if (it != categoryOverrides.end())
        return (Category) juce::jlimit (0, numCategories - 1, it->second);

    return guessCategory (d);
}

void PluginLibrary::setCategoryOverride (const juce::String& key, Category c)
{
    categoryOverrides[key] = (int) c;

    for (auto& f : families)
        if (f.key == key)
            f.category = c;

    save();
}

//==============================================================================
void PluginLibrary::setFavourite (const juce::String& key, bool shouldBe)
{
    if (shouldBe) favourites.addIfNotAlreadyThere (key);
    else          favourites.removeString (key);
    save();
}

void PluginLibrary::noteUsed (const juce::String& key)
{
    recents.removeString (key);
    recents.insert (0, key);
    while (recents.size() > 12)
        recents.remove (recents.size() - 1);
    save();
}

int PluginLibrary::getGainReductionParameterOverride (const juce::String& key) const
{
    const auto it = grParameterOverrides.find (key);
    return it != grParameterOverrides.end() ? it->second : -1;
}

void PluginLibrary::setGainReductionParameterOverride (const juce::String& key, int parameterIndex)
{
    if (parameterIndex < 0) grParameterOverrides.erase (key);
    else                    grParameterOverrides[key] = parameterIndex;
    save();
}

bool PluginLibrary::getShowEstimatedGR (const juce::String& key, Category c) const
{
    const auto it = estimateOverrides.find (key);
    return it != estimateOverrides.end() ? it->second != 0 : c == Category::dynamics;
}

void PluginLibrary::setShowEstimatedGR (const juce::String& key, bool shouldShow)
{
    estimateOverrides[key] = shouldShow ? 1 : 0;
    save();
}

//==============================================================================
void PluginLibrary::save() const
{
    juce::XmlElement xml ("PLUGINLIBRARY");

    auto* fav = xml.createNewChildElement ("FAVOURITES");
    for (auto& k : favourites) fav->createNewChildElement ("P")->setAttribute ("key", k);

    auto* rec = xml.createNewChildElement ("RECENTS");
    for (auto& k : recents) rec->createNewChildElement ("P")->setAttribute ("key", k);

    auto writeMap = [&xml] (const char* tag, const std::map<juce::String, int>& m)
    {
        auto* e = xml.createNewChildElement (tag);
        for (auto& [k, v] : m)
        {
            auto* p = e->createNewChildElement ("P");
            p->setAttribute ("key", k);
            p->setAttribute ("value", v);
        }
    };

    writeMap ("CATEGORIES", categoryOverrides);
    writeMap ("GRPARAMS", grParameterOverrides);
    writeMap ("ESTIMATES", estimateOverrides);

    xml.writeTo (file);
}

void PluginLibrary::load()
{
    auto xml = juce::parseXML (file);
    if (xml == nullptr)
        return;

    if (auto* e = xml->getChildByName ("FAVOURITES"))
        for (auto* p : e->getChildIterator()) favourites.add (p->getStringAttribute ("key"));

    if (auto* e = xml->getChildByName ("RECENTS"))
        for (auto* p : e->getChildIterator()) recents.add (p->getStringAttribute ("key"));

    auto readMap = [&xml] (const char* tag, std::map<juce::String, int>& m)
    {
        if (auto* e = xml->getChildByName (tag))
            for (auto* p : e->getChildIterator())
                m[p->getStringAttribute ("key")] = p->getIntAttribute ("value");
    };

    readMap ("CATEGORIES", categoryOverrides);
    readMap ("GRPARAMS", grParameterOverrides);
    readMap ("ESTIMATES", estimateOverrides);
}
