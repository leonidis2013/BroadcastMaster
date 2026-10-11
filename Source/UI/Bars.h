// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki
#pragma once
#include <memory>
#include "Theme.h"
#include "ParamControls.h"
#include "../PluginProcessor.h"
#include "../PresetManager.h"

namespace bm::ui
{
// Presets, bypass A/B and latency mode.
class TopBar : public juce::Component
{
public:
    TopBar (BroadcastMasterProcessor& p, bm::PresetManager& pm) : proc (p), presets (pm)
    {
        presetBox.setTextWhenNothingSelected ("Preset");
        presetBox.setTooltip ("Factory and saved presets. A preset covers the stage settings and switches, not the loudness target or lip-sync delay.");
        presetBox.onChange = [this] { choosePreset(); };
        addAndMakeVisible (presetBox);

        saveButton.setTooltip ("Save the current settings as a preset");
        saveButton.onClick = [this] { askSave(); };
        deleteButton.setTooltip ("Delete the selected saved preset");
        deleteButton.onClick = [this] { askDelete(); };
        addAndMakeVisible (saveButton);
        addAndMakeVisible (deleteButton);

        bypassButton.setClickingTogglesState (true);
        bypassButton.setTooltip ("Compare with the untouched input (key: B). The bypassed signal is delayed to line up with the processed one.");
        bypassAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, bm::params::bypass, bypassButton);
        addAndMakeVisible (bypassButton);

        matchToggle.setTooltip ("When bypassed, bring the input to the same loudness as the processed signal, so a louder result does not just sound better.");
        matchAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, bm::params::matchBypass, matchToggle);
        addAndMakeVisible (matchToggle);

        latencyBox.addItemList ({ "Live  (lowest latency)", "Broadcast  (linear-phase EQ, longer look-ahead)" }, 1);
        latencyBox.setTooltip ("Live: minimum-phase EQ and 1 ms limiter look-ahead.\nBroadcast: linear-phase EQ and 5 ms look-ahead, adds latency.");
        latencyAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, bm::params::latencyMode, latencyBox);
        addAndMakeVisible (latencyBox);

        refresh();
    }

    void refresh()
    {
        const auto current = presets.currentName();
        userNames = presets.userPresetNames();
        presetBox.clear (juce::dontSendNotification);
        presetBox.addSectionHeading ("Factory");
        const auto& all = bm::factoryPresets();
        for (size_t i = 0; i < all.size(); ++i)
            presetBox.addItem (all[i].name, (int) i + 1);
        if (userNames.size() > 0)
        {
            presetBox.addSeparator();
            presetBox.addSectionHeading ("My presets");
            for (int i = 0; i < userNames.size(); ++i)
                presetBox.addItem (userNames[i], 1000 + i);
        }
        shownName = current;
        presetBox.setText (current.isEmpty() ? juce::String() : current, juce::dontSendNotification);
        deleteButton.setEnabled (userNames.contains (current));
    }

    void tick()
    {
        if (presets.currentName() != shownName) refresh();

        const int lat = proc.reportedLatency();
        const double ms = lat * 1000.0 / juce::jmax (1.0, proc.getCurrentSampleRate());
        const bool bypassed = bypassButton.getToggleState();
        juce::String t = "Latency " + juce::String (ms, 1) + " ms";
        if (bypassed && matchToggle.getToggleState())
            t = "Bypass match " + signedText (-proc.bypassMatchOffsetDb()) + " dB    " + t;
        if (t != info) { info = t; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (pal().textDim);
        g.setFont (monoFont (11.0f));
        g.drawText (info, infoArea, juce::Justification::centredRight);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        presetBox.setBounds (r.removeFromLeft (230).withSizeKeepingCentre (230, 28));
        r.removeFromLeft (6);
        saveButton.setBounds (r.removeFromLeft (58).withSizeKeepingCentre (58, 28));
        r.removeFromLeft (4);
        deleteButton.setBounds (r.removeFromLeft (62).withSizeKeepingCentre (62, 28));
        r.removeFromLeft (22);
        bypassButton.setBounds (r.removeFromLeft (92).withSizeKeepingCentre (92, 28));
        r.removeFromLeft (8);
        matchToggle.setBounds (r.removeFromLeft (150).withSizeKeepingCentre (150, 28));
        r.removeFromLeft (14);
        latencyBox.setBounds (r.removeFromLeft (300).withSizeKeepingCentre (300, 28));
        r.removeFromLeft (10);
        infoArea = r;
    }

private:
    void choosePreset()
    {
        const int id = presetBox.getSelectedId();
        if (id <= 0) return;
        if (id < 1000) presets.applyFactory (id - 1);
        else if (id - 1000 < userNames.size()) presets.applyUser (userNames[id - 1000]);
        shownName = presets.currentName();
        deleteButton.setEnabled (userNames.contains (shownName));
    }

    void askSave()
    {
        auto* w = new juce::AlertWindow ("Save preset", "Name for this preset:", juce::MessageBoxIconType::NoIcon);
        const auto cur = presets.currentName();
        w->addTextEditor ("name", userNames.contains (cur) ? cur : juce::String(), "");
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<TopBar> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
        {
            if (result != 1 || safe == nullptr) return;
            const auto name = w->getTextEditorContents ("name");
            if (safe->presets.saveUser (name)) safe->refresh();
        }), true);
    }

    void askDelete()
    {
        const auto name = presets.currentName();
        if (! userNames.contains (name)) return;
        juce::Component::SafePointer<TopBar> safe (this);
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                          .withTitle ("Delete preset")
                                          .withMessage ("Delete the saved preset \"" + name + "\"?")
                                          .withButton ("Delete").withButton ("Cancel"),
                                      [safe, name] (int result)
                                      {
                                          if (result != 1 || safe == nullptr) return;
                                          if (safe->presets.deleteUser (name))
                                          {
                                              safe->presets.setCurrentName ({});
                                              safe->refresh();
                                          }
                                      });
    }

    BroadcastMasterProcessor& proc;
    bm::PresetManager& presets;
    juce::ComboBox presetBox, latencyBox;
    juce::TextButton saveButton { "Save" }, deleteButton { "Delete" }, bypassButton { "BYPASS" };
    juce::ToggleButton matchToggle { "Loudness-matched" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttach, matchAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> latencyAttach;
    juce::StringArray userNames;
    juce::String shownName, info;
    juce::Rectangle<int> infoArea;
};

// Solo / monitor bus: source A and B, A/B switch, level, and the solo-to-main fallback.
class MonitorBar : public juce::Component
{
public:
    explicit MonitorBar (BroadcastMasterProcessor& p) : proc (p)
    {
        soloButton.setClickingTogglesState (true);
        soloButton.setTooltip ("Send the monitor signal to the second (solo) output (key: S)");
        soloAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, bm::params::soloOn, soloButton);
        addAndMakeVisible (soloButton);

        setupCombo (comboA, bm::params::monA, comboAAttach, "Source A for the solo bus");
        setupCombo (comboB, bm::params::monB, comboBAttach, "Source B for the solo bus");

        for (auto* b : { &buttonA, &buttonB })
        {
            b->setClickingTogglesState (true);
            b->setRadioGroupId (202);
            addAndMakeVisible (*b);
        }
        buttonA.setTooltip ("Monitor source A (key: 1)");
        buttonB.setTooltip ("Monitor source B (key: 2)");
        buttonA.onClick = [this] { if (buttonA.getToggleState()) setParamPlain (proc.apvts, bm::params::monAB, 0.0f); };
        buttonB.onClick = [this] { if (buttonB.getToggleState()) setParamPlain (proc.apvts, bm::params::monAB, 1.0f); };

        level.setSliderStyle (juce::Slider::LinearHorizontal);
        level.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
        level.setTextValueSuffix (" dB");
        level.setNumDecimalPlacesToDisplay (1);
        level.setDoubleClickReturnValue (true, 0.0);
        level.setTooltip ("Monitor level (solo bus only)");
        levelAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, bm::params::monLevel, level);
        addAndMakeVisible (level);

        toMain.setTooltip ("Use only when the host cannot route a second output (OBS cannot): the solo signal REPLACES the main output while solo is on. "
                           "Never leave this on while you are on air.");
        toMainAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, bm::params::soloToMain, toMain);
        addAndMakeVisible (toMain);
    }

    void tick()
    {
        const bool b = param (bm::params::monAB) > 0.5f;
        buttonA.setToggleState (! b, juce::dontSendNotification);
        buttonB.setToggleState (b, juce::dontSendNotification);

        const bool connected = proc.isSoloBusConnected();
        const bool solo = param (bm::params::soloOn) > 0.5f;
        const bool main = param (bm::params::soloToMain) > 0.5f;
        juce::String t; juce::Colour c = pal().textDim;
        if (connected)            { t = solo ? "Solo bus: playing" : "Solo bus: connected";             c = pal().good; }
        else if (solo && main)    { t = "WARNING: the main output is replaced by the solo signal";    c = pal().bad; }
        else if (main)            { t = "No solo output connected. Fallback armed (replaces main)";    c = pal().warn; }
        else if (solo)            { t = "No solo output connected. Enable Solo to main to hear it";    c = pal().warn; }
        else                      { t = "No solo output connected (OBS cannot route one)";            }
        if (t != status || c != statusColour) { status = t; statusColour = c; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (pal().textDim);
        g.setFont (font (11.0f, true));
        g.drawText ("A", labelA, juce::Justification::centredRight);
        g.drawText ("B", labelB, juce::Justification::centredRight);
        g.drawText ("LEVEL", labelLevel, juce::Justification::centredRight);
        g.setColour (statusColour);
        g.setFont (font (11.5f, true));
        g.drawFittedText (status, statusArea, juce::Justification::centredRight, 2, 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        soloButton.setBounds (r.removeFromLeft (84).withSizeKeepingCentre (84, 28));
        r.removeFromLeft (12);
        labelA = r.removeFromLeft (14);
        comboA.setBounds (r.removeFromLeft (180).withSizeKeepingCentre (180, 28));
        r.removeFromLeft (10);
        labelB = r.removeFromLeft (14);
        comboB.setBounds (r.removeFromLeft (180).withSizeKeepingCentre (180, 28));
        r.removeFromLeft (10);
        buttonA.setBounds (r.removeFromLeft (34).withSizeKeepingCentre (34, 28));
        r.removeFromLeft (2);
        buttonB.setBounds (r.removeFromLeft (34).withSizeKeepingCentre (34, 28));
        r.removeFromLeft (14);
        labelLevel = r.removeFromLeft (44);
        level.setBounds (r.removeFromLeft (170).withSizeKeepingCentre (170, 24));
        r.removeFromLeft (10);
        toMain.setBounds (r.removeFromLeft (130).withSizeKeepingCentre (130, 28));
        r.removeFromLeft (10);
        statusArea = r;
    }

private:
    void setupCombo (juce::ComboBox& box, const char* id, std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& attach,
                     const juce::String& tip)
    {
        box.addItemList (bm::params::monitorSourceNames(), 1);
        box.setTooltip (tip);
        addAndMakeVisible (box);
        attach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, box);
    }

    float param (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    BroadcastMasterProcessor& proc;
    juce::TextButton soloButton { "SOLO" }, buttonA { "A" }, buttonB { "B" };
    juce::ComboBox comboA, comboB;
    juce::Slider level;
    juce::ToggleButton toMain { "Solo to main" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAttach, toMainAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAAttach, comboBAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> levelAttach;
    juce::Rectangle<int> labelA, labelB, labelLevel, statusArea;
    juce::String status;
    juce::Colour statusColour = juce::Colours::grey;
};
} // namespace bm::ui
