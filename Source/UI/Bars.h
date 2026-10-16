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
// Small non-modal pop-up for saving a preset: a name field (pre-filled with a free name, so Save works even if the
// host does not pass keystrokes through), overwrite of the current saved preset, and file export / import.
class SavePresetPanel : public juce::Component
{
public:
    std::function<void (const juce::String&)> onSave;
    std::function<void()> onExport, onImport;

    SavePresetPanel (const juce::String& suggestion, const juce::String& overwriteName)
    {
        title.setText ("Save current settings as a preset", juce::dontSendNotification);
        title.setFont (font (12.0f, true));
        addAndMakeVisible (title);
        edit.setText (suggestion, false);
        edit.setSelectAllWhenFocused (true);
        edit.setTextToShowWhenEmpty ("Preset name", pal().textDim);
        edit.onReturnKey = [this] { doSave(); };
        addAndMakeVisible (edit);
        save.setButtonText ("Save");
        save.onClick = [this] { doSave(); };
        addAndMakeVisible (save);
        if (overwriteName.isNotEmpty())
        {
            overwrite.setButtonText ("Overwrite \"" + overwriteName + "\"");
            overwrite.onClick = [this, overwriteName] { if (onSave) onSave (overwriteName); };
            addAndMakeVisible (overwrite);
        }
        exportButton.setButtonText ("Export...");
        exportButton.setTooltip ("Save the current settings to a .bmpreset file you can copy to another computer");
        exportButton.onClick = [this] { if (onExport) onExport(); };
        importButton.setButtonText ("Import...");
        importButton.setTooltip ("Load a .bmpreset file into My presets");
        importButton.onClick = [this] { if (onImport) onImport(); };
        addAndMakeVisible (exportButton);
        addAndMakeVisible (importButton);
        setSize (320, overwriteName.isNotEmpty() ? 150 : 118);
    }

    void visibilityChanged() override { if (isShowing()) edit.grabKeyboardFocus(); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        title.setBounds (r.removeFromTop (18));
        r.removeFromTop (6);
        auto row = r.removeFromTop (28);
        save.setBounds (row.removeFromRight (64));
        row.removeFromRight (6);
        edit.setBounds (row);
        r.removeFromTop (6);
        if (overwrite.isVisible()) { overwrite.setBounds (r.removeFromTop (28)); r.removeFromTop (6); }
        auto row2 = r.removeFromTop (28);
        exportButton.setBounds (row2.removeFromLeft (row2.getWidth() / 2 - 3));
        row2.removeFromLeft (6);
        importButton.setBounds (row2);
    }

private:
    void doSave() { if (onSave) onSave (edit.getText()); }

    juce::Label title;
    juce::TextEditor edit;
    juce::TextButton save, overwrite, exportButton, importButton;
};

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

        saveButton.setTooltip ("Save the current settings as a preset (also overwrite, export and import)");
        saveButton.onClick = [this] { askSave(); };
        deleteButton.setTooltip ("Delete the selected saved preset");
        deleteButton.onClick = [this] { askDelete(); };
        addAndMakeVisible (saveButton);
        addAndMakeVisible (deleteButton);

        panicButton.setTooltip ("Panic: clear the leveler gain and the limiter at once, and restart the meters. Use it if something sounds wrong mid-service.");
        panicButton.onClick = [this] { proc.requestPanic(); };
        addAndMakeVisible (panicButton);

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
        presetBox.setBounds (r.removeFromLeft (210).withSizeKeepingCentre (210, 28));
        r.removeFromLeft (6);
        saveButton.setBounds (r.removeFromLeft (58).withSizeKeepingCentre (58, 28));
        r.removeFromLeft (4);
        deleteButton.setBounds (r.removeFromLeft (62).withSizeKeepingCentre (62, 28));
        r.removeFromLeft (4);
        panicButton.setBounds (r.removeFromLeft (62).withSizeKeepingCentre (62, 28));
        r.removeFromLeft (18);
        bypassButton.setBounds (r.removeFromLeft (92).withSizeKeepingCentre (92, 28));
        r.removeFromLeft (8);
        matchToggle.setBounds (r.removeFromLeft (150).withSizeKeepingCentre (150, 28));
        r.removeFromLeft (14);
        latencyBox.setBounds (r.removeFromLeft (270).withSizeKeepingCentre (270, 28));
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

    juce::String freeName() const
    {
        for (int i = 1; i < 1000; ++i)
        {
            const auto n = "My Preset " + juce::String (i);
            if (! userNames.contains (n)) return n;
        }
        return "My Preset";
    }

    void askSave()
    {
        const auto cur = presets.currentName();
        const bool onUser = userNames.contains (cur);
        auto panel = std::make_unique<SavePresetPanel> (onUser ? cur : freeName(), onUser ? cur : juce::String());
        juce::Component::SafePointer<TopBar> safe (this);
        juce::Component::SafePointer<SavePresetPanel> sp (panel.get());
        panel->onSave = [safe, sp] (const juce::String& name)
        {
            if (safe == nullptr) return;
            if (safe->presets.saveUser (name)) { safe->proc.refreshProgramList(); safe->refresh(); }
            if (sp != nullptr)
                if (auto* box = sp->findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
        };
        panel->onExport = [safe, sp]
        {
            if (safe == nullptr) return;
            if (sp != nullptr)
                if (auto* box = sp->findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
            safe->exportPreset();
        };
        panel->onImport = [safe, sp]
        {
            if (safe == nullptr) return;
            if (sp != nullptr)
                if (auto* box = sp->findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
            safe->importPreset();
        };
        juce::CallOutBox::launchAsynchronously (std::move (panel), saveButton.getScreenBounds(), nullptr);
    }

    void exportPreset()
    {
        const auto base = presets.currentName().isEmpty() ? juce::String ("BroadcastMaster preset") : presets.currentName();
        chooser = std::make_unique<juce::FileChooser> ("Export preset", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                                             .getChildFile (juce::File::createLegalFileName (base) + ".bmpreset"), "*.bmpreset");
        juce::Component::SafePointer<TopBar> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe] (const juce::FileChooser& fc)
                              {
                                  if (safe == nullptr || fc.getResult() == juce::File()) return;
                                  safe->presets.exportTo (fc.getResult().withFileExtension ("bmpreset"));
                              });
    }

    void importPreset()
    {
        chooser = std::make_unique<juce::FileChooser> ("Import preset", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.bmpreset");
        juce::Component::SafePointer<TopBar> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe] (const juce::FileChooser& fc)
                              {
                                  if (safe == nullptr || fc.getResult() == juce::File()) return;
                                  if (safe->presets.importFrom (fc.getResult())) { safe->proc.refreshProgramList(); safe->refresh(); }
                              });
    }

    void askDelete()
    {
        const auto name = presets.currentName();
        if (! userNames.contains (name)) return;
        juce::Component::SafePointer<TopBar> safe (this);
        // OK/Cancel box: the callback gets 1 for "Delete" and 0 for "Cancel" in every JUCE version
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete preset",
                                            "Delete the saved preset \"" + name + "\"?", "Delete", "Cancel", nullptr,
                                            juce::ModalCallbackFunction::create ([safe, name] (int result)
                                            {
                                                if (result != 1 || safe == nullptr) return;
                                                if (safe->presets.deleteUser (name))
                                                {
                                                    safe->presets.setCurrentName ({});
                                                    safe->proc.refreshProgramList();
                                                    safe->refresh();
                                                }
                                            }));
    }

    BroadcastMasterProcessor& proc;
    bm::PresetManager& presets;
    juce::ComboBox presetBox, latencyBox;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TextButton saveButton { "Save" }, deleteButton { "Delete" }, panicButton { "Panic" }, bypassButton { "BYPASS" };
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
