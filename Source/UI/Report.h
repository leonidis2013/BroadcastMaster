// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki
#pragma once
#include <memory>
#include "../PluginProcessor.h"
#include "../Meters/LoudnessPresets.h"

namespace bm::ui
{
// Builds the text of a loudness report (CSV) from the output meters since the last Reset.
inline juce::String makeLoudnessReport (BroadcastMasterProcessor& proc)
{
    auto raw = [&] (const char* id) { if (auto* a = proc.apvts.getRawParameterValue (id)) return a->load(); return 0.0f; };

    const int idx = juce::jlimit (0, bm::kNumLoudnessPresets - 1, (int) std::lround (raw (bm::params::loudPreset)));
    const auto& preset = bm::kLoudnessPresets[idx];
    const bool custom = idx == bm::kCustomPresetIndex;
    const float target = custom ? raw (bm::params::customTarget) : preset.targetLufs;
    const float ceiling = custom ? raw (bm::params::customCeiling) : preset.ceilingDbtp;
    const float tol = preset.tolerance;

    const auto s = proc.metersOut.snapshot();
    auto num = [] (float v, int d = 1) { return v <= -119.0f ? juce::String ("n/a") : juce::String (v, d); };
    auto verdict = [] (bool known, bool ok) { return known ? (ok ? "PASS" : "FAIL") : "n/a"; };

    const bool haveInt = s.integrated > -119.0f, haveTp = s.tpHold > -119.0f;
    const bool intOk = haveInt && std::fabs (s.integrated - target) <= tol;
    const bool tpOk = haveTp && s.tpHold <= ceiling + 0.05f;

    juce::String t;
    t << "BroadcastMaster loudness report\n";
    t << "Generated," << juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S") << "\n";
    t << "Plug-in version," << JucePlugin_VersionString << "\n";
    t << "Measured at,plug-in output (after the limiter), since the last Reset\n\n";
    t << "Destination," << juce::String (preset.name) << "\n";
    t << "Target (LUFS)," << juce::String (target, 1) << "\n";
    t << "Tolerance (+/- LU)," << juce::String (tol, 1) << "\n";
    t << "True-peak ceiling (dBTP)," << juce::String (ceiling, 1) << "\n\n";
    t << "Measurement,Value,Unit,Result\n";
    t << "Integrated loudness," << num (s.integrated) << ",LUFS," << verdict (haveInt, intOk) << "\n";
    t << "Loudness range," << (haveInt ? juce::String (s.lra, 1) : juce::String ("n/a")) << ",LU,\n";
    t << "Max short-term," << num (s.maxShortTerm) << ",LUFS,\n";
    t << "Max momentary," << num (s.maxMomentary) << ",LUFS,\n";
    t << "True peak (hold)," << num (s.tpHold) << ",dBTP," << verdict (haveTp, tpOk) << "\n";
    t << "Sample peak," << num (s.samplePeak) << ",dBFS,\n";
    t << "Peak-to-loudness ratio," << (s.hasPlr ? juce::String (s.plr, 1) : juce::String ("n/a")) << ",dB,\n";
    return t;
}

// Asks where to save, then writes the report.
inline void saveLoudnessReport (BroadcastMasterProcessor& proc)
{
    auto chooser = std::make_shared<juce::FileChooser> ("Save loudness report",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("BroadcastMaster loudness report " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M") + ".csv"),
        "*.csv");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [chooser, &proc] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f == juce::File()) return;
                              f.withFileExtension ("csv").replaceWithText (makeLoudnessReport (proc));
                          });
}
} // namespace bm::ui
