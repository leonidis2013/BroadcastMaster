// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki
#include "PluginEditor.h"
#include "UI/SettingsPanel.h"

using namespace bm::ui;

BroadcastMasterEditor::BroadcastMasterEditor (BroadcastMasterProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      spectrum (p), loudness (p.apvts, [&p] { p.resetMeters(); }), stereo (p), dynamics (p), chain (p),
      presets (p.apvts), topBar (p, presets), monBar (p), modules (p)
{
    setLookAndFeel (&laf);
    setWantsKeyboardFocus (true);

    addAndMakeVisible (spectrum);
    addAndMakeVisible (loudness);
    addAndMakeVisible (stereo);
    addAndMakeVisible (dynamics);
    addAndMakeVisible (chain);
    addAndMakeVisible (topBar);
    addAndMakeVisible (monBar);
    addAndMakeVisible (modules);

    chain.onSelect = [this] (int s) { selectStage (s); };
    selectStage ((int) p.apvts.state.getProperty ("selectedStage", bm::dsp::sEq));

    inputButton.setClickingTogglesState (true);
    outputButton.setClickingTogglesState (true);
    inputButton.setRadioGroupId (101);
    outputButton.setRadioGroupId (101);
    inputButton.setTooltip ("Meter the signal entering the chain (key: I)");
    outputButton.setTooltip ("Meter the signal leaving the chain (key: O, Tab flips)");
    inputButton.onClick  = [this] { if (inputButton.getToggleState())  setSource (false); };
    outputButton.onClick = [this] { if (outputButton.getToggleState()) setSource (true); };
    addAndMakeVisible (inputButton);
    addAndMakeVisible (outputButton);

    compareButton.setTooltip ("Show the change the chain makes: delta readouts and the other signal's spectrum overlaid");
    compareAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, bm::params::compare, compareButton);
    addAndMakeVisible (compareButton);

    themeButton.onClick = [this] { toggleTheme(); };
    settingsButton.onClick = [this] { openSettings(); };
    settingsButton.setTooltip ("Spectrum options, custom loudness target, target link, theme, latency and presets folder");
    addAndMakeVisible (themeButton);
    addAndMakeVisible (settingsButton);

    applyTheme (param (bm::params::theme) < 0.5f);

    setResizable (true, true);
    setResizeLimits (1100, 860, 1900, 1400);
    setSize (1240, 1020);

    startTimerHz (20);
}

BroadcastMasterEditor::~BroadcastMasterEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void BroadcastMasterEditor::applyTheme (bool dark)
{
    appliedTheme = dark ? 0 : 1;
    currentPalette() = dark ? &darkPalette() : &lightPalette();
    laf.applyPalette();
    themeButton.setButtonText (dark ? "Light" : "Dark");
    sendLookAndFeelChange();
    repaint();
}

void BroadcastMasterEditor::toggleTheme()
{
    if (auto* prm = processor.apvts.getParameter (bm::params::theme))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (appliedTheme == 0 ? 1.0f : 0.0f);
        prm->endChangeGesture();
    }
}

void BroadcastMasterEditor::setSource (bool output)
{
    if (auto* prm = processor.apvts.getParameter (bm::params::meterSource))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (output ? 1.0f : 0.0f);
        prm->endChangeGesture();
    }
}

void BroadcastMasterEditor::openSettings()
{
    juce::CallOutBox::launchAsynchronously (std::make_unique<SettingsPanel> (processor), settingsButton.getBounds(), this);
}

void BroadcastMasterEditor::selectStage (int stage)
{
    modules.select (stage);
    chain.setSelected (modules.getSelected());
    processor.apvts.state.setProperty ("selectedStage", modules.getSelected(), nullptr);
}

void BroadcastMasterEditor::toggleParam (const char* id)
{
    bm::ui::setParamPlain (processor.apvts, id, param (id) > 0.5f ? 0.0f : 1.0f);
}

bool BroadcastMasterEditor::keyPressed (const juce::KeyPress& key)
{
    if (key.getTextCharacter() == 'b' || key.getTextCharacter() == 'B') { toggleParam (bm::params::bypass); return true; }
    if (key.getTextCharacter() == 's' || key.getTextCharacter() == 'S') { toggleParam (bm::params::soloOn); return true; }
    if (key.getTextCharacter() == '1') { bm::ui::setParamPlain (processor.apvts, bm::params::monAB, 0.0f); return true; }
    if (key.getTextCharacter() == '2') { bm::ui::setParamPlain (processor.apvts, bm::params::monAB, 1.0f); return true; }
    if (key == juce::KeyPress::tabKey) { setSource (param (bm::params::meterSource) < 0.5f); return true; }
    if (key.getTextCharacter() == 'i' || key.getTextCharacter() == 'I') { setSource (false); return true; }
    if (key.getTextCharacter() == 'o' || key.getTextCharacter() == 'O') { setSource (true); return true; }
    return false;
}

void BroadcastMasterEditor::timerCallback()
{
    if (! isShowing()) return; // hidden editor (host window closed or tab inactive): no work

    // Follow host automation / state changes for the theme and the source switch.
    const int themeNow = param (bm::params::theme) < 0.5f ? 0 : 1;
    if (themeNow != appliedTheme)
        applyTheme (themeNow == 0);

    UiState st;
    st.in = processor.metersIn.snapshot();
    st.out = processor.metersOut.snapshot();
    st.showOutput = param (bm::params::meterSource) > 0.5f;
    st.compare = param (bm::params::compare) > 0.5f;

    inputButton.setToggleState (! st.showOutput, juce::dontSendNotification);
    outputButton.setToggleState (st.showOutput, juce::dontSendNotification);

    st.presetIdx = juce::jlimit (0, bm::kNumLoudnessPresets - 1, (int) std::lround (param (bm::params::loudPreset)));
    const auto& preset = bm::kLoudnessPresets[st.presetIdx];
    if (st.presetIdx == bm::kCustomPresetIndex)
    {
        st.target = param (bm::params::customTarget);
        st.ceiling = param (bm::params::customCeiling);
        st.tolerance = preset.tolerance;
    }
    else
    {
        st.target = preset.targetLufs;
        st.ceiling = preset.ceilingDbtp;
        st.tolerance = preset.tolerance;
    }

    static constexpr int resolutions[] = { 3, 6, 12 };
    spectrum.setSettings ((bm::Weighting) juce::jlimit (0, 2, (int) std::lround (param (bm::params::rtaWeighting))),
                          (int) std::lround (param (bm::params::rtaTarget)),
                          resolutions[juce::jlimit (0, 2, (int) std::lround (param (bm::params::rtaResolution)))]);

    spectrum.tick (st);
    loudness.tick (st);
    stereo.tick (st);
    dynamics.tick (st);
    chain.tick();
    modules.tick();
    topBar.tick();
    monBar.tick();
    spectrum.setEqOverlay (modules.getSelected() == bm::dsp::sEq, processor.readEq());
    spectrum.setMbcOverlay (modules.getSelected() == bm::dsp::sMbc);
}

void BroadcastMasterEditor::paint (juce::Graphics& g)
{
    const auto& p = pal();
    g.fillAll (p.bg);

    auto header = getLocalBounds().reduced (14, 10).removeFromTop (34);

    // "on air" lamp and title
    g.setColour (p.accent);
    g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ (float) header.getX() + 8.0f, (float) header.getCentreY() }));
    g.setColour (p.text);
    g.setFont (font (19.0f, true));
    g.drawText ("BroadcastMaster", header.withTrimmedLeft (24).withWidth (190), juce::Justification::centredLeft);
    g.setColour (p.textDim);
    g.setFont (monoFont (11.0f));
    g.drawText ("v" + juce::String (JucePlugin_VersionString), header.withTrimmedLeft (24 + 160).withWidth (70), juce::Justification::centredLeft);
}

void BroadcastMasterEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto header = r.removeFromTop (40);
    r.removeFromTop (6);

    // header controls
    auto h = header.withTrimmedLeft (262);
    inputButton.setBounds (h.removeFromLeft (84).withSizeKeepingCentre (84, 28));
    h.removeFromLeft (2);
    outputButton.setBounds (h.removeFromLeft (84).withSizeKeepingCentre (84, 28));
    h.removeFromLeft (16);
    compareButton.setBounds (h.removeFromLeft (150).withSizeKeepingCentre (150, 28));
    settingsButton.setBounds (h.removeFromRight (86).withSizeKeepingCentre (86, 28));
    h.removeFromRight (8);
    themeButton.setBounds (h.removeFromRight (64).withSizeKeepingCentre (64, 28));

    topBar.setBounds (r.removeFromTop (36));
    r.removeFromTop (2);
    monBar.setBounds (r.removeFromTop (36));
    r.removeFromTop (8);

    modules.setBounds (r.removeFromBottom (206));
    r.removeFromBottom (8);
    chain.setBounds (r.removeFromBottom (178));
    r.removeFromBottom (8);
    dynamics.setBounds (r.removeFromBottom (76));
    r.removeFromBottom (8);

    auto rightCol = r.removeFromRight (juce::jlimit (380, 480, r.getWidth() * 36 / 100));
    r.removeFromRight (8);
    spectrum.setBounds (r);
    loudness.setBounds (rightCol.removeFromTop (rightCol.getHeight() * 55 / 100));
    rightCol.removeFromTop (8);
    stereo.setBounds (rightCol);
}
