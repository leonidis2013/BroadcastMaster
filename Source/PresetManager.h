#pragma once
#include <map>
#include <set>
#include "Parameters.h"

namespace bm
{
// Factory presets are written as differences from the defaults, so they stay readable.
struct FactoryPreset
{
    const char* name;
    std::vector<std::pair<std::string, float>> values;
};

inline const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets
    {
        { "Init (flat, limiter only)", {} },

        { "Talk / Podcast",
          { { "en_hp", 1 }, { "hp_freq", 40 },
            { "en_phase", 1 }, { "ph_freq", 160 }, { "ph_stages", 2 },
            { "en_eq", 1 }, { "eq2_gain", -2.0f }, { "eq4_gain", 2.0f }, { "eq6_gain", 1.5f },
            { "en_sat", 1 }, { "sat_type", 0 }, { "sat_amt", 15 },
            { "en_mbc", 1 },
            { "mb1_thr", -30 }, { "mb1_ratio", 2.5f }, { "mb1_atk", 30 }, { "mb1_rel", 250 },
            { "mb2_thr", -26 }, { "mb2_ratio", 3.0f }, { "mb2_atk", 20 }, { "mb2_rel", 200 },
            { "mb3_thr", -24 }, { "mb3_ratio", 3.0f }, { "mb3_atk", 10 }, { "mb3_rel", 150 },
            { "mb4_thr", -28 }, { "mb4_ratio", 2.0f }, { "mb4_atk", 5 },  { "mb4_rel", 120 },
            { "mb_knee", 8 },
            { "en_lev", 1 }, { "lv_mode", 1 }, { "lv_range", 9 }, { "lv_speed", 1 },
            { "lm_rel", 80 } } },

        { "Music Show",
          { { "en_hp", 1 }, { "hp_freq", 25 },
            { "en_eq", 1 }, { "eq1_gain", 1.0f }, { "eq6_gain", 1.0f },
            { "en_sat", 1 }, { "sat_type", 1 }, { "sat_amt", 20 },
            { "en_exc", 1 }, { "exc_amt", 15 }, { "bass_amt", 20 },
            { "en_mbc", 1 },
            { "mb1_thr", -26 }, { "mb2_thr", -24 }, { "mb3_thr", -24 }, { "mb4_thr", -26 },
            { "mb1_ratio", 1.8f }, { "mb2_ratio", 1.8f }, { "mb3_ratio", 1.8f }, { "mb4_ratio", 1.8f },
            { "mb1_atk", 30 }, { "mb2_atk", 30 }, { "mb3_atk", 30 }, { "mb4_atk", 30 },
            { "mb1_rel", 300 }, { "mb2_rel", 300 }, { "mb3_rel", 300 }, { "mb4_rel", 300 },
            { "mb_knee", 10 },
            { "en_lev", 1 }, { "lv_mode", 2 }, { "lv_speed", 0 }, { "lv_range", 6 },
            { "en_clip", 1 }, { "cl_thr", -1.5f }, { "cl_knee", 60 },
            { "lm_rel", 150 } } },

        { "Mixed Show (talk + music)",
          { { "en_hp", 1 }, { "hp_freq", 30 },
            { "en_phase", 1 }, { "ph_stages", 1 },
            { "en_eq", 1 }, { "eq2_gain", -1.5f }, { "eq4_gain", 1.5f }, { "eq6_gain", 1.0f },
            { "en_sat", 1 }, { "sat_type", 0 }, { "sat_amt", 10 },
            { "en_mbc", 1 },
            { "mb1_thr", -28 }, { "mb2_thr", -25 }, { "mb3_thr", -24 }, { "mb4_thr", -27 },
            { "mb1_ratio", 2.2f }, { "mb2_ratio", 2.2f }, { "mb3_ratio", 2.2f }, { "mb4_ratio", 2.2f },
            { "mb1_atk", 20 }, { "mb2_atk", 20 }, { "mb3_atk", 20 }, { "mb4_atk", 20 },
            { "mb1_rel", 220 }, { "mb2_rel", 220 }, { "mb3_rel", 220 }, { "mb4_rel", 220 },
            { "mb_knee", 8 },
            { "en_lev", 1 }, { "lv_mode", 0 }, { "lv_speed", 1 }, { "lv_range", 8 },
            { "en_clip", 1 }, { "cl_thr", -2.0f }, { "cl_knee", 50 },
            { "lm_rel", 100 } } },

        { "Gentle Leveler + Safety Limiter",
          { { "en_hp", 1 }, { "hp_freq", 30 },
            { "en_lev", 1 }, { "lv_mode", 0 }, { "lv_speed", 0 }, { "lv_range", 6 },
            { "lm_rel", 120 } } },
    };
    return presets;
}

// Applies and stores presets. A preset holds the stage gains and switches and every module setting, but not the
// limiter ceiling or leveler target (those follow the loudness target), the lip-sync delay, or global switches.
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s) {}

    static juce::File userFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("BroadcastMaster").getChildFile ("Presets");
    }

    juce::String currentName() const { return apvts.state.getProperty ("presetName").toString(); }
    void setCurrentName (const juce::String& n) { apvts.state.setProperty ("presetName", n, nullptr); }

    juce::StringArray userPresetNames() const
    {
        juce::StringArray names;
        const auto dir = userFolder();
        if (dir.isDirectory())
            for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.bmpreset"))
                names.add (f.getFileNameWithoutExtension());
        names.sort (true);
        return names;
    }

    bool isFactory (const juce::String& name) const
    {
        for (const auto& p : factoryPresets()) if (name == p.name) return true;
        return false;
    }

    bool applyFactory (int index)
    {
        const auto& all = factoryPresets();
        if (index < 0 || index >= (int) all.size()) return false;
        std::map<std::string, float> v;
        for (const auto& kv : all[(size_t) index].values) v[kv.first] = kv.second;
        applyValues (v);
        setCurrentName (all[(size_t) index].name);
        return true;
    }

    bool applyUser (const juce::String& name)
    {
        const auto xml = juce::XmlDocument::parse (fileFor (name));
        if (xml == nullptr || ! xml->hasTagName ("BroadcastMasterPreset")) return false;
        std::map<std::string, float> v;
        for (auto* e : xml->getChildWithTagNameIterator ("P"))
            v[e->getStringAttribute ("id").toStdString()] = (float) e->getDoubleAttribute ("v");
        applyValues (v);
        setCurrentName (name);
        return true;
    }

    bool saveUser (juce::String name)
    {
        name = juce::File::createLegalFileName (name.trim());
        if (name.isEmpty()) return false;
        if (isFactory (name)) name += " (user)";

        juce::XmlElement root ("BroadcastMasterPreset");
        root.setAttribute ("version", 1);
        root.setAttribute ("name", name);
        for (const auto& id : bm::params::presetParamIds())
            if (auto* a = apvts.getRawParameterValue (id))
            {
                auto* e = root.createNewChildElement ("P");
                e->setAttribute ("id", juce::String (id));
                e->setAttribute ("v", (double) a->load());
            }

        const auto dir = userFolder();
        if (! dir.createDirectory().wasOk()) return false;
        if (! root.writeTo (fileFor (name))) return false;
        setCurrentName (name);
        return true;
    }

    bool deleteUser (const juce::String& name)
    {
        const auto f = fileFor (name);
        return f.existsAsFile() && f.deleteFile();
    }

private:
    juce::File fileFor (const juce::String& name) const
    {
        return userFolder().getChildFile (juce::File::createLegalFileName (name) + ".bmpreset");
    }

    static std::map<std::string, float> defaultValues()
    {
        std::map<std::string, float> d;
        for (int i = 0; i < bm::params::kNumStages; ++i)
        {
            const auto& s = bm::params::kStages[i];
            if (! s.isLimiter) d[s.id] = s.defDb;
            if (s.enableId) d[s.enableId] = s.defaultOn ? 1.0f : 0.0f;
        }
        for (const auto& s : bm::params::moduleSpecs()) d[s.id] = s.def;
        return d;
    }

    // Everything the preset covers goes to its default first, so a preset never inherits leftovers.
    void applyValues (const std::map<std::string, float>& values)
    {
        const auto defaults = defaultValues();
        for (const auto& id : bm::params::presetParamIds())
        {
            float v = 0.0f;
            const auto it = values.find (id);
            if (it != values.end()) v = it->second;
            else
            {
                const auto d = defaults.find (id);
                if (d == defaults.end()) continue;
                v = d->second;
            }
            if (auto* prm = apvts.getParameter (id))
                prm->setValueNotifyingHost (prm->convertTo0to1 (v));
        }
    }

    juce::AudioProcessorValueTreeState& apvts;
};
} // namespace bm
