// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki
#pragma once
#include <memory>
#include "PluginProcessor.h"
#include "UI/BMLookAndFeel.h"
#include "UI/SpectrumPanel.h"
#include "UI/LoudnessPanel.h"
#include "UI/StereoPanel.h"
#include "UI/DynamicsStrip.h"
#include "UI/ChainStrip.h"
#include "UI/Bars.h"
#include "UI/ModulePanels.h"
#include "PresetManager.h"
#include "UI/UiState.h"

class BroadcastMasterEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BroadcastMasterEditor (BroadcastMasterProcessor&);
    ~BroadcastMasterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }

private:
    void timerCallback() override;
    void applyTheme (bool dark);
    void setSource (bool output);
    void toggleTheme();
    void openSettings();
    void selectStage (int stage);
    void toggleParam (const char* id);
    float param (const char* id) const { return processor.apvts.getRawParameterValue (id)->load(); }

    BroadcastMasterProcessor& processor;
    bm::ui::BMLookAndFeel laf;

    bm::ui::SpectrumPanel spectrum;
    bm::ui::LoudnessPanel loudness;
    bm::ui::StereoPanel stereo;
    bm::ui::DynamicsStrip dynamics;
    bm::ui::ChainStrip chain;
    bm::PresetManager presets;
    bm::ui::TopBar topBar;
    bm::ui::MonitorBar monBar;
    bm::ui::ModulePanel modules;

    juce::TextButton inputButton { "INPUT" }, outputButton { "OUTPUT" }, themeButton, settingsButton { "Settings" };
    juce::ToggleButton compareButton { "Compare (delta)" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> compareAttach;
    juce::TooltipWindow tooltip { this, 600 };

    int appliedTheme = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BroadcastMasterEditor)
};
