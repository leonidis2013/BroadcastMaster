// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki
#pragma once
#include <string>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Dsp/Chain.h"
#include "Meters/LoudnessPresets.h"

namespace bm::params
{
// ---- global parameter IDs ------------------------------------------------------------------------------------
inline constexpr const char* meterSource   = "meter_source";   // 0 = chain input, 1 = chain output
inline constexpr const char* compare       = "compare";        // delta readouts + RTA overlay
inline constexpr const char* loudPreset    = "loud_preset";
inline constexpr const char* customTarget  = "custom_target";
inline constexpr const char* customCeiling = "custom_ceiling";
inline constexpr const char* rtaWeighting  = "rta_weight";
inline constexpr const char* rtaTarget     = "rta_target";
inline constexpr const char* rtaResolution = "rta_res";
inline constexpr const char* theme         = "theme";          // 0 = dark, 1 = light

inline constexpr const char* latencyMode   = "latency_mode";   // 0 = Live, 1 = Broadcast
inline constexpr const char* bypass        = "bypass";
inline constexpr const char* matchBypass   = "match_bypass";
inline constexpr const char* presetLink    = "preset_link";    // loudness target drives leveler target + limiter ceiling
inline constexpr const char* soloOn        = "solo_on";
inline constexpr const char* monA          = "mon_a";
inline constexpr const char* monB          = "mon_b";
inline constexpr const char* monAB         = "mon_ab";         // 0 = A, 1 = B
inline constexpr const char* monLevel      = "mon_level";
inline constexpr const char* soloToMain    = "solo_to_main";

// ---- the stages ----------------------------------------------------------------------------------------------
static_assert (bm::dsp::kStageCount == 12, "stage table below must match the chain");

struct StageInfo
{
    const char* id;        // output gain parameter
    const char* name;
    const char* gainLabel;
    float minDb, maxDb, defDb;
    bool isLimiter;        // the limiter's "output gain" is its ceiling
    const char* enableId;  // nullptr = always on
    bool defaultOn;
};

inline constexpr StageInfo kStages[] =
{
    { "g_in",   "Input Trim",        "Trim",    -12.0f, 12.0f,  0.0f, false, nullptr,    true  },
    { "g_hp",   "Subsonic HPF",      "Gain",    -12.0f, 12.0f,  0.0f, false, "en_hp",    true  },
    { "g_phase","Phase Rotation",    "Gain",    -12.0f, 12.0f,  0.0f, false, "en_phase", false },
    { "g_eq",   "Tonal EQ",          "Gain",    -12.0f, 12.0f,  0.0f, false, "en_eq",    true  },
    { "g_sat",  "Saturation",        "Gain",    -12.0f, 12.0f,  0.0f, false, "en_sat",   false },
    { "g_exc",  "Exciter / Bass",    "Gain",    -12.0f, 12.0f,  0.0f, false, "en_exc",   false },
    { "g_mbc",  "Multiband Comp",    "Gain",    -12.0f, 12.0f,  0.0f, false, "en_mbc",   false },
    { "g_lev",  "Auto Leveler",      "Gain",    -12.0f, 12.0f,  0.0f, false, "en_lev",   false },
    { "g_st",   "Stereo Utilities",  "Gain",    -12.0f, 12.0f,  0.0f, false, "en_st",    true  },
    { "g_clip", "Soft Clipper",      "Gain",    -12.0f, 12.0f,  0.0f, false, "en_clip",  false },
    { "g_lim",  "True-Peak Limiter", "Ceiling", -12.0f,  0.0f, -1.0f, true,  "en_lim",   true  },
    { "g_out",  "Output Trim",       "Trim",    -24.0f,  0.0f,  0.0f, false, nullptr,    true  },
};

inline constexpr int kNumStages = bm::dsp::kStageCount;
inline constexpr int kLimiterStage = bm::dsp::sLim;

inline juce::StringArray presetNames()
{
    juce::StringArray names;
    for (const auto& p : bm::kLoudnessPresets) names.add (p.name);
    return names;
}

inline juce::StringArray monitorSourceNames()
{
    return { "Output cue", "Input cue", "Delta (what changed)", "Limiter delta", "MBC band 1", "MBC band 2", "MBC band 3", "MBC band 4" };
}

// ---- module parameters, described once and used for the layout, the generic panel and presets ---------------
enum class Kind { Float, Int, Bool, Choice };

struct ParamSpec
{
    std::string id, name, unit, group;
    Kind kind = Kind::Float;
    float min = 0.0f, max = 1.0f, def = 0.0f, step = 0.0f;
    float centre = 0.0f;           // value shown at the middle of a skewed slider (0 = linear)
    juce::StringArray choices;
    int stage = -1;                // chain stage it belongs to (-1 = global)
    bool custom = false;           // drawn by a custom panel (EQ, multiband), not the generic one
    bool inPreset = true;          // stored in presets
};

inline std::string eqId (int band, const char* what) { return "eq" + std::to_string (band + 1) + "_" + what; }
inline std::string mbId (int band, const char* what) { return "mb" + std::to_string (band + 1) + "_" + what; }

inline const std::vector<ParamSpec>& moduleSpecs()
{
    static const std::vector<ParamSpec> specs = []
    {
        std::vector<ParamSpec> v;
        using bm::dsp::EqParams;
        using bm::dsp::MbcParams;

        auto flt = [&] (const std::string& id, const char* name, const char* unit, int stage, float mn, float mx, float def,
                        float step, float centre = 0.0f, const char* group = "", bool custom = false, bool preset = true)
        {
            ParamSpec s; s.id = id; s.name = name; s.unit = unit; s.group = group; s.kind = Kind::Float;
            s.min = mn; s.max = mx; s.def = def; s.step = step; s.centre = centre; s.stage = stage; s.custom = custom; s.inPreset = preset;
            v.push_back (s);
        };
        auto integer = [&] (const std::string& id, const char* name, int stage, int mn, int mx, int def, const char* group = "")
        {
            ParamSpec s; s.id = id; s.name = name; s.group = group; s.kind = Kind::Int;
            s.min = (float) mn; s.max = (float) mx; s.def = (float) def; s.step = 1.0f; s.stage = stage;
            v.push_back (s);
        };
        auto boolean = [&] (const std::string& id, const char* name, int stage, bool def, const char* group = "", bool custom = false, bool preset = true)
        {
            ParamSpec s; s.id = id; s.name = name; s.group = group; s.kind = Kind::Bool; s.def = def ? 1.0f : 0.0f;
            s.stage = stage; s.custom = custom; s.inPreset = preset;
            v.push_back (s);
        };
        auto choice = [&] (const std::string& id, const char* name, int stage, juce::StringArray c, int def, const char* group = "",
                           bool custom = false)
        {
            ParamSpec s; s.id = id; s.name = name; s.group = group; s.kind = Kind::Choice; s.choices = c;
            s.max = (float) (c.size() - 1); s.def = (float) def; s.step = 1.0f; s.stage = stage; s.custom = custom;
            v.push_back (s);
        };

        using namespace bm::dsp;

        // 1 subsonic high-pass
        flt ("hp_freq", "Cutoff", "Hz", sHpf, 10.0f, 60.0f, 25.0f, 1.0f);

        // 2 phase rotation
        flt ("ph_freq", "Frequency", "Hz", sPhase, 40.0f, 1000.0f, 160.0f, 1.0f, 200.0f);
        integer ("ph_stages", "Amount (stages)", sPhase, 1, 4, 2);

        // 3 tonal EQ: six bands and a tilt
        {
            const EqParams def;
            for (int i = 0; i < EqParams::kBands; ++i)
            {
                const std::string n = "Band " + std::to_string (i + 1) + " ";
                boolean (eqId (i, "on"), (n + "On").c_str(), sEq, true, "", true);
                flt (eqId (i, "freq"), (n + "Freq").c_str(), "Hz", sEq, 20.0f, 20000.0f, def.band[i].freq, 1.0f, 1000.0f, "", true);
                flt (eqId (i, "gain"), (n + "Gain").c_str(), "dB", sEq, -12.0f, 12.0f, 0.0f, 0.1f, 0.0f, "", true);
                flt (eqId (i, "q"), (n + (i == 0 || i == EqParams::kBands - 1 ? "Slope" : "Q")).c_str(), "", sEq, 0.3f, 8.0f, def.band[i].q, 0.01f, 1.0f, "", true);
                choice (eqId (i, "tgt"), (n + "Target").c_str(), sEq, { "Stereo", "Mid", "Side" }, 0, "", true);
            }
            flt ("eq_tilt", "Tilt", "dB", sEq, -6.0f, 6.0f, 0.0f, 0.1f, 0.0f, "", true);
        }

        // 4 saturation
        choice ("sat_type", "Type", sSat, { "Tube", "Tape", "Transformer" }, 0);
        flt ("sat_amt", "Amount", "%", sSat, 0.0f, 100.0f, 25.0f, 1.0f);
        flt ("sat_mix", "Mix", "%", sSat, 0.0f, 100.0f, 100.0f, 1.0f);

        // 5 exciter and bass enhancer
        flt ("exc_freq", "Frequency", "Hz", sExc, 1500.0f, 12000.0f, 4500.0f, 10.0f, 5000.0f, "Exciter (air)");
        flt ("exc_amt", "Amount", "%", sExc, 0.0f, 100.0f, 20.0f, 1.0f, 0.0f, "Exciter (air)");
        flt ("bass_freq", "Frequency", "Hz", sExc, 30.0f, 200.0f, 80.0f, 1.0f, 80.0f, "Bass enhancer");
        flt ("bass_amt", "Amount", "%", sExc, 0.0f, 100.0f, 20.0f, 1.0f, 0.0f, "Bass enhancer");

        // 6 multiband compressor
        {
            const MbcParams def;
            flt ("mb_x1", "Crossover 1", "Hz", sMbc, 30.0f, 400.0f, def.x[0], 1.0f, 120.0f, "", true);
            flt ("mb_x2", "Crossover 2", "Hz", sMbc, 300.0f, 3000.0f, def.x[1], 1.0f, 800.0f, "", true);
            flt ("mb_x3", "Crossover 3", "Hz", sMbc, 2000.0f, 12000.0f, def.x[2], 1.0f, 5000.0f, "", true);
            for (int b = 0; b < MbcParams::kBands; ++b)
            {
                const std::string n = "Band " + std::to_string (b + 1) + " ";
                boolean (mbId (b, "on"), (n + "On").c_str(), sMbc, true, "", true);
                flt (mbId (b, "thr"), (n + "Threshold").c_str(), "dB", sMbc, -60.0f, 0.0f, def.band[b].thrDb, 0.1f, 0.0f, "", true);
                flt (mbId (b, "ratio"), (n + "Ratio").c_str(), ":1", sMbc, 1.0f, 20.0f, def.band[b].ratio, 0.1f, 4.0f, "", true);
                flt (mbId (b, "atk"), (n + "Attack").c_str(), "ms", sMbc, 1.0f, 300.0f, def.band[b].attackMs, 0.1f, 30.0f, "", true);
                flt (mbId (b, "rel"), (n + "Release").c_str(), "ms", sMbc, 20.0f, 2000.0f, def.band[b].releaseMs, 1.0f, 200.0f, "", true);
                flt (mbId (b, "mk"), (n + "Makeup").c_str(), "dB", sMbc, -12.0f, 12.0f, 0.0f, 0.1f, 0.0f, "", true);
            }
            flt ("mb_knee", "Knee", "dB", sMbc, 0.0f, 24.0f, def.kneeDb, 0.1f, 0.0f, "", true);
            flt ("mb_mix", "Mix", "%", sMbc, 0.0f, 100.0f, 100.0f, 1.0f, 0.0f, "", true);
        }

        // 7 auto leveler (the target follows the loudness preset when the link is on, so it is not stored in presets)
        flt ("lv_target", "Target", "LUFS", sLev, -30.0f, -6.0f, -14.0f, 0.1f, 0.0f, "", false, false);
        flt ("lv_range", "Range (max +/-)", "dB", sLev, 0.0f, 18.0f, 9.0f, 0.1f);
        choice ("lv_speed", "Speed", sLev, { "Slow", "Medium", "Fast" }, 1);
        flt ("lv_gate", "Gate (freeze below)", "LUFS", sLev, -70.0f, -30.0f, -50.0f, 0.5f);
        choice ("lv_mode", "Content", sLev, { "Auto", "Speech", "Music" }, 0);

        // 8 stereo utilities (the lip-sync delay belongs to the set-up, not to a sound preset)
        flt ("st_width", "Width", "%", sStereo, 0.0f, 200.0f, 100.0f, 1.0f);
        flt ("st_mid", "Mid gain", "dB", sStereo, -12.0f, 12.0f, 0.0f, 0.1f);
        flt ("st_side", "Side gain", "dB", sStereo, -12.0f, 12.0f, 0.0f, 0.1f);
        flt ("st_exc", "Amount", "%", sStereo, 0.0f, 100.0f, 0.0f, 1.0f, 0.0f, "Stereo exciter");
        flt ("st_exc_f", "Frequency", "Hz", sStereo, 1500.0f, 12000.0f, 4000.0f, 10.0f, 5000.0f, "Stereo exciter");
        boolean ("st_flipl", "Polarity L", sStereo, false);
        boolean ("st_flipr", "Polarity R", sStereo, false);
        boolean ("st_mono", "Mono sum", sStereo, false);
        flt ("st_delay", "Lip-sync delay", "ms", sStereo, 0.0f, 500.0f, 0.0f, 0.1f, 50.0f, "", false, false);

        // 9 soft clipper
        flt ("cl_thr", "Threshold", "dB", sClip, -12.0f, 0.0f, -2.0f, 0.1f);
        flt ("cl_knee", "Linear zone", "%", sClip, 0.0f, 95.0f, 50.0f, 1.0f);

        // 10 true-peak limiter (the ceiling is the stage gain)
        flt ("lm_rel", "Release", "ms", sLim, 5.0f, 1000.0f, 100.0f, 1.0f, 100.0f);

        return v;
    }();
    return specs;
}

inline const ParamSpec* findSpec (const std::string& id)
{
    for (const auto& s : moduleSpecs()) if (s.id == id) return &s;
    return nullptr;
}

// Everything a user preset stores: stage gains and enables, plus every module parameter marked inPreset.
// (Not the ceiling, which belongs to the loudness target; not global switches.)
inline std::vector<std::string> presetParamIds()
{
    std::vector<std::string> ids;
    for (int i = 0; i < kNumStages; ++i)
    {
        if (! kStages[i].isLimiter) ids.push_back (kStages[i].id);
        if (kStages[i].enableId) ids.push_back (kStages[i].enableId);
    }
    for (const auto& s : moduleSpecs()) if (s.inPreset) ids.push_back (s.id);
    return ids;
}

inline std::unique_ptr<juce::RangedAudioParameter> makeParameter (const ParamSpec& s)
{
    using juce::ParameterID;
    const ParameterID pid { s.id, 1 };
    const auto attrs = juce::AudioParameterFloatAttributes().withLabel (s.unit);
    switch (s.kind)
    {
        case Kind::Bool:   return std::make_unique<juce::AudioParameterBool> (pid, s.name, s.def > 0.5f);
        case Kind::Choice: return std::make_unique<juce::AudioParameterChoice> (pid, s.name, s.choices, (int) s.def);
        case Kind::Int:    return std::make_unique<juce::AudioParameterInt> (pid, s.name, (int) s.min, (int) s.max, (int) s.def);
        default:
        {
            juce::NormalisableRange<float> r (s.min, s.max, s.step);
            if (s.centre > s.min && s.centre < s.max) r.setSkewForCentre (s.centre);
            return std::make_unique<juce::AudioParameterFloat> (pid, s.name, r, s.def, attrs);
        }
    }
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using juce::ParameterID;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { meterSource, 1 }, "Meter Source",
                                                              juce::StringArray { "Input", "Output" }, 1));
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { compare, 1 }, "Compare (Delta)", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { loudPreset, 1 }, "Loudness Target",
                                                              presetNames(), 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { customTarget, 1 }, "Custom Target",
                                                             juce::NormalisableRange<float> (-36.0f, -6.0f, 0.1f), -16.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("LUFS")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { customCeiling, 1 }, "Custom True-Peak Ceiling",
                                                             juce::NormalisableRange<float> (-6.0f, 0.0f, 0.1f), -1.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("dBTP")));
    layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { rtaWeighting, 1 }, "RTA Weighting",
                                                              juce::StringArray { "Flat", "A", "C" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { rtaTarget, 1 }, "RTA Target Curve",
                                                              juce::StringArray { "Off", "Pink -3 dB/oct", "Pink -4.5 dB/oct" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { rtaResolution, 1 }, "RTA Resolution",
                                                              juce::StringArray { "1/3 octave", "1/6 octave", "1/12 octave" }, 1));
    layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { theme, 1 }, "Theme",
                                                              juce::StringArray { "Dark", "Light" }, 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { latencyMode, 1 }, "Latency Mode",
                                                              juce::StringArray { "Live", "Broadcast" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { bypass, 1 }, "Bypass", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { matchBypass, 1 }, "Loudness-Matched Bypass", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { presetLink, 1 }, "Link Target To Leveler + Limiter", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { soloOn, 1 }, "Solo Bus On", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { monA, 1 }, "Monitor A", monitorSourceNames(), 1));
    layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { monB, 1 }, "Monitor B", monitorSourceNames(), 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (ParameterID { monAB, 1 }, "Monitor A/B", juce::StringArray { "A", "B" }, 1));
    layout.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { monLevel, 1 }, "Monitor Level",
                                                             juce::NormalisableRange<float> (-40.0f, 6.0f, 0.1f), 0.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { soloToMain, 1 }, "Solo To Main Output", false));

    for (const auto& s : kStages)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (ParameterID { s.id, 1 },
                                                                 juce::String (s.name) + " " + s.gainLabel,
                                                                 juce::NormalisableRange<float> (s.minDb, s.maxDb, 0.1f), s.defDb,
                                                                 juce::AudioParameterFloatAttributes().withLabel ("dB")));
        if (s.enableId)
            layout.add (std::make_unique<juce::AudioParameterBool> (ParameterID { s.enableId, 1 }, juce::String (s.name) + " On", s.defaultOn));
    }

    for (const auto& s : moduleSpecs())
        layout.add (makeParameter (s));

    return layout;
}
} // namespace bm::params
