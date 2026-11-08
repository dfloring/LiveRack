#include "GainReduction.h"

#if JUCE_PLUGINHOST_VST3
 #include <pluginterfaces/base/funknown.h>
 #include <pluginterfaces/vst/ivstcomponent.h>
 #include <pluginterfaces/vst/ivsteditcontroller.h>

// PreSonus Plug-In Extensions (public domain): IGainReductionInfo, implemented
// by the plugin's VST3 edit controller
namespace Presonus
{
    struct IGainReductionInfo : public Steinberg::FUnknown
    {
        virtual double PLUGIN_API getGainReductionValueInDb() = 0;
    };

    static const Steinberg::TUID IGainReductionInfo_iid = INLINE_UID (0x8e3c292c, 0x95924f9d, 0xb2590b1e, 0x100e4198);
}
#endif

namespace GainReduction
{

juce::String Probe::describe (juce::AudioProcessor& plugin) const
{
    auto paramName = [&plugin] (int index)
    {
        const auto& params = plugin.getParameters();
        return juce::isPositiveAndBelow (index, params.size())
                 ? "#" + juce::String (index) + " \"" + params[index]->getName (64) + "\""
                 : juce::String ("?");
    };

    switch (source)
    {
        case Source::userParameter: return "Parameter " + paramName (parameterIndex) + " (chosen by you)";
        case Source::presonus:      return "PreSonus gain-reduction extension (VST3)";
        case Source::parameter:     return "Meter parameter " + paramName (parameterIndex);
        case Source::none:          break;
    }
    return "None found. Dynamics plugins show an estimate from level in vs out.";
}

int findMeterParameter (juce::AudioProcessor& plugin)
{
    const auto& params = plugin.getParameters();

    // Parameters the plugin tagged as a gain-reduction meter
    for (int i = 0; i < params.size(); ++i)
    {
        const auto category = params[i]->getCategory();
        if (category == juce::AudioProcessorParameter::compressorLimiterGainReductionMeter
            || category == juce::AudioProcessorParameter::expanderGateGainReductionMeter)
            return i;
    }

    // Otherwise by name
    for (int i = 0; i < params.size(); ++i)
    {
        const auto name = params[i]->getName (64).toLowerCase().removeCharacters (" _-.:");

        if (name.contains ("gainreduction") || name == "gr" || name.startsWith ("grmeter")
            || (name.endsWith ("gr") && name.length() <= 4) || name == "reduction"
            || name == "compressionamount") // Apple AUDynamicsProcessor
            return i;
    }

    return -1;
}

bool readPresonus (juce::AudioPluginInstance& plugin, float& db)
{
   #if JUCE_PLUGINHOST_VST3
    struct Visitor : public juce::ExtensionsVisitor
    {
        bool ok = false;
        double value = 0.0;

        void visitVST3Client (const VST3Client& client) override
        {
            auto* component = client.getIComponentPtr();
            if (component == nullptr)
                return;

            Presonus::IGainReductionInfo* info = nullptr;

            // Single-component plugins implement it on the component itself;
            // otherwise ask the edit controller it exposes
            if (component->queryInterface (Presonus::IGainReductionInfo_iid, (void**) &info) != Steinberg::kResultOk || info == nullptr)
            {
                info = nullptr;
                Steinberg::Vst::IEditController* controller = nullptr;

                if (component->queryInterface (Steinberg::Vst::IEditController_iid, (void**) &controller) == Steinberg::kResultOk
                    && controller != nullptr)
                {
                    if (controller->queryInterface (Presonus::IGainReductionInfo_iid, (void**) &info) != Steinberg::kResultOk)
                        info = nullptr;
                    controller->release();
                }
            }

            if (info != nullptr)
            {
                value = info->getGainReductionValueInDb();
                info->release();
                ok = true;
            }
        }
    } visitor;

    plugin.getExtensions (visitor);

    if (visitor.ok)
        db = (float) std::abs (visitor.value);

    return visitor.ok;
   #else
    juce::ignoreUnused (plugin, db);
    return false;
   #endif
}

float parameterToDb (juce::AudioProcessorParameter& p)
{
    const auto text = p.getCurrentValueAsText().trim();

    // A meter that displays itself in dB: trust its text
    if (p.getLabel().containsIgnoreCase ("db") || text.containsIgnoreCase ("db"))
    {
        if (text.containsIgnoreCase ("inf"))
            return muted;

        const auto numeric = text.retainCharacters ("0123456789.-");
        if (numeric.containsAnyOf ("0123456789"))
            return std::abs (numeric.getFloatValue());
    }

    // Otherwise the standard meter convention (Pro Tools, SSL 360): 0 = no
    // reduction, 1 = reduced all the way to silence
    const float v = juce::jlimit (0.0f, 1.0f, p.getValue());

    if (v >= 0.9999f)
        return muted;

    return -20.0f * std::log10 (1.0f - v);
}

Probe probe (juce::AudioPluginInstance& plugin, int userParameterIndex)
{
    Probe result;

    if (juce::isPositiveAndBelow (userParameterIndex, plugin.getParameters().size()))
    {
        result.source = Source::userParameter;
        result.parameterIndex = userParameterIndex;
        return result;
    }

    float unused = 0.0f;
    if (readPresonus (plugin, unused))
    {
        result.source = Source::presonus;
        return result;
    }

    result.parameterIndex = findMeterParameter (plugin);
    result.source = result.parameterIndex >= 0 ? Source::parameter : Source::none;
    return result;
}

bool read (juce::AudioPluginInstance& plugin, const Probe& p, float& db)
{
    switch (p.source)
    {
        case Source::presonus:
            return readPresonus (plugin, db);

        case Source::userParameter:
        case Source::parameter:
        {
            const auto& params = plugin.getParameters();
            if (! juce::isPositiveAndBelow (p.parameterIndex, params.size()))
                return false;

            db = parameterToDb (*params[p.parameterIndex]);
            return true;
        }

        case Source::none:
            break;
    }

    return false;
}

} // namespace GainReduction
