// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki
#pragma once
#include <memory>
#include "Theme.h"
#include "../PluginProcessor.h"
#include "../PresetManager.h"
#include "Report.h"

namespace bm::ui
{
// The pop-up box for options that are rarely changed, keeping the main window for meters and the chain.
class SettingsPanel : public juce::Component
{
public:
    explicit SettingsPanel (BroadcastMasterProcessor& p) : proc (p)
    {
        addCombo (rtaWeightBox, "RTA weighting", bm::params::rtaWeighting, { "Flat", "A", "C" }, rtaWeightAttach);
        addCombo (rtaTargetBox, "RTA target curve", bm::params::rtaTarget, { "Off", "Pink -3 dB/oct", "Pink -4.5 dB/oct" }, rtaTargetAttach);
        addCombo (rtaResBox, "RTA resolution", bm::params::rtaResolution, { "1/3 octave", "1/6 octave", "1/12 octave" }, rtaResAttach);
        addCombo (themeBox, "Theme", bm::params::theme, { "Dark", "Light" }, themeAttach);

        addSlider (targetSlider, "Custom loudness target", bm::params::customTarget, " LUFS", targetAttach);
        addSlider (ceilingSlider, "Custom true-peak ceiling", bm::params::customCeiling, " dBTP", ceilingAttach);

        linkToggle.setTooltip ("When on, choosing a loudness target also sets the leveler target and the limiter ceiling to match.");
        linkAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, bm::params::presetLink, linkToggle);
        addAndMakeVisible (linkToggle);
        labels.push_back ({ "Loudness target drives", 36 + rows * 36 });
        controls.push_back (&linkToggle);
        ++rows;

        folderButton.onClick = [] { bm::PresetManager::userFolder().createDirectory(); bm::PresetManager::userFolder().revealToUser(); };
        folderButton.setTooltip ("Open the folder where your saved presets live");
        addAndMakeVisible (folderButton);

        reportButton.onClick = [this] { saveLoudnessReport (proc); };
        reportButton.setTooltip ("Save integrated loudness, loudness range, peaks and a pass/fail against the chosen destination as a CSV file (since the last Reset)");
        addAndMakeVisible (reportButton);

        exportButton.onClick = [this] { chooseSettingsFile (true); };
        exportButton.setTooltip ("Save every setting of the plugin (all stages, loudness target, ceiling, delay, view options) to a file");
        importButton.onClick = [this] { chooseSettingsFile (false); };
        importButton.setTooltip ("Load a settings file saved with Export. This replaces the current settings.");
        addAndMakeVisible (exportButton);
        addAndMakeVisible (importButton);

        const int lat = p.reportedLatency();
        const double ms = lat * 1000.0 / juce::jmax (1.0, p.getCurrentSampleRate());
        latencyText = juce::String (lat) + " samples  (" + juce::String (ms, 1) + " ms)";
        soloText = p.isSoloBusConnected() ? "connected" : "not connected";
        versionText = juce::String (JucePlugin_Name) + " " + JucePlugin_VersionString;

        setSize (400, 36 + rows * 36 + 182);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        g.fillAll (p.panel);
        g.setColour (p.textDim);
        g.setFont (font (11.0f, true));
        g.drawText ("SETTINGS", getLocalBounds().reduced (14, 0).withHeight (30), juce::Justification::centredLeft);

        g.setFont (font (12.0f));
        for (const auto& l : labels)
        {
            g.setColour (p.text);
            g.drawText (l.text, 14, l.y, 160, 28, juce::Justification::centredLeft);
        }

        const int y = 36 + rows * 36;
        g.setColour (p.text);
        g.drawText ("Reported latency", 14, y, 160, 24, juce::Justification::centredLeft);
        g.drawText ("Solo output", 14, y + 24, 160, 24, juce::Justification::centredLeft);
        g.setColour (p.textDim);
        g.setFont (monoFont (12.0f));
        g.drawText (latencyText, 184, y, 202, 24, juce::Justification::centredLeft);
        g.drawText (soloText, 184, y + 24, 202, 24, juce::Justification::centredLeft);
        g.setFont (font (12.0f));
        g.setColour (p.text);
        g.drawText ("Loudness report", 14, 36 + rows * 36 + 84, 160, 26, juce::Justification::centredLeft);
        g.drawText ("All settings", 14, 36 + rows * 36 + 120, 160, 26, juce::Justification::centredLeft);
        g.setColour (p.textDim);
        g.setFont (font (11.0f));
        g.drawText (versionText, 14, getHeight() - 26, getWidth() - 28, 20, juce::Justification::centredLeft);
    }

    void resized() override
    {
        int y = 36;
        for (auto* c : controls)
        {
            c->setBounds (184, y, 202, 28);
            y += 36;
        }
        folderButton.setBounds (184, 36 + rows * 36 + 52, 202, 26);
        reportButton.setBounds (184, 36 + rows * 36 + 84, 202, 26);
        exportButton.setBounds (184, 36 + rows * 36 + 120, 98, 26);
        importButton.setBounds (288, 36 + rows * 36 + 120, 98, 26);
    }

private:
    // The chooser is owned by the callback, so it survives this pop-up closing while the file dialog is open.
    void chooseSettingsFile (bool save)
    {
        auto chooser = std::make_shared<juce::FileChooser> (save ? "Export all settings" : "Import all settings",
                           juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                               .getChildFile (save ? "BroadcastMaster settings.bmsettings" : ""), "*.bmsettings");
        auto* procPtr = &proc;
        const int flags = save ? (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting)
                               : (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles);
        chooser->launchAsync (flags, [chooser, procPtr, save] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f == juce::File()) return;
            if (save) procPtr->exportSettings (f.withFileExtension ("bmsettings"));
            else      procPtr->importSettings (f);
        });
    }

    struct Label { juce::String text; int y; };

    void addCombo (juce::ComboBox& box, const juce::String& name, const char* paramId, const juce::StringArray& items,
                   std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& attach)
    {
        box.addItemList (items, 1);
        addAndMakeVisible (box);
        attach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, paramId, box);
        labels.push_back ({ name, 36 + rows * 36 });
        controls.push_back (&box);
        ++rows;
    }

    void addSlider (juce::Slider& s, const juce::String& name, const char* paramId, const juce::String& suffix,
                    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attach)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 78, 24);
        s.setTextValueSuffix (suffix);
        addAndMakeVisible (s);
        attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, s);
        labels.push_back ({ name, 36 + rows * 36 });
        controls.push_back (&s);
        ++rows;
    }

    BroadcastMasterProcessor& proc;
    juce::ComboBox rtaWeightBox, rtaTargetBox, rtaResBox, themeBox;
    juce::Slider targetSlider, ceilingSlider;
    juce::ToggleButton linkToggle { "Leveler target + limiter ceiling" };
    juce::TextButton folderButton { "Open presets folder" }, reportButton { "Save loudness report..." },
                      exportButton { "Export..." }, importButton { "Import..." };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rtaWeightAttach, rtaTargetAttach, rtaResAttach, themeAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> targetAttach, ceilingAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> linkAttach;
    std::vector<Label> labels;
    std::vector<juce::Component*> controls;
    int rows = 0;
    juce::String latencyText, soloText, versionText;
};
} // namespace bm::ui
