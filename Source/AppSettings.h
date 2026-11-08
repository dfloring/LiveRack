#pragma once
#include "RackEngine.h"

// Small key/value store for window layout (positions, two-window mode)
namespace AppSettings
{
    inline juce::File file() { return RackEngine::getSettingsDir().getChildFile ("layout.xml"); }

    inline juce::String get (const juce::String& key)
    {
        auto xml = juce::parseXML (file());
        return xml != nullptr ? xml->getStringAttribute (key) : juce::String();
    }

    inline void set (const juce::String& key, const juce::String& value)
    {
        auto xml = juce::parseXML (file());
        if (xml == nullptr)
            xml = std::make_unique<juce::XmlElement> ("LAYOUT");

        xml->setAttribute (key, value);
        xml->writeTo (file());
    }
}
