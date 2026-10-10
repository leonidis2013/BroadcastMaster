#pragma once
#include <array>
#include <functional>
#include <memory>
#include "Theme.h"
#include "UiState.h"
#include "../PluginProcessor.h"

namespace bm::ui
{
// One tile per chain stage: name, on/off switch, gain-staging knob and a mini level meter showing the level at that
// stage's output. Clicking a tile opens its settings in the panel below.
class StageTile : public juce::Component
{
public:
    StageTile (int index, BroadcastMasterProcessor& p, std::function<void (int)> onSelectStage)
        : stage (index), proc (p), onSelect (std::move (onSelectStage))
    {
        const auto& info = bm::params::kStages[index];

        knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
        knob.setTextValueSuffix (" dB");
        knob.setNumDecimalPlacesToDisplay (1);
        knob.setDoubleClickReturnValue (true, info.defDb);
        knob.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        addAndMakeVisible (knob);
        knob.addMouseListener (this, false); // a click on the knob also selects the stage
        attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, info.id, knob);

        if (info.isLimiter)
            knob.setTooltip ("Limiter ceiling: the true-peak level the output never exceeds. Follows the loudness target when linked. Double-click to reset.");
        else if (index == bm::dsp::sOut)
            knob.setTooltip ("Cut-only output trim after the limiter. Double-click to reset.");
        else
            knob.setTooltip (juce::String (info.name) + " output gain, for gain staging. Double-click to reset.");

        if (info.enableId)
        {
            onButton.setClickingTogglesState (true);
            onButton.setTooltip ("Switch this stage on or off. Latency never changes.");
            onAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, info.enableId, onButton);
            addAndMakeVisible (onButton);
            enableParam = p.apvts.getRawParameterValue (info.enableId);
        }
    }

    void setSelected (bool s) { if (s != selected) { selected = s; repaint(); } }

    void tick()
    {
        const float pk = proc.stagePeakDb[(size_t) stage].load (std::memory_order_relaxed);
        const float rms = proc.stageRmsDb[(size_t) stage].load (std::memory_order_relaxed);
        peakHold = pk > peakHold ? pk : juce::jmax (pk, peakHold - 1.2f);
        rmsShown += (rms - rmsShown) * 0.35f;
        repaint();
    }

    bool isOn() const noexcept { return enableParam == nullptr || enableParam->load() > 0.5f; }

    void mouseDown (const juce::MouseEvent&) override { if (onSelect) onSelect (stage); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 0);
        onButton.setBounds (juce::Rectangle<int> (getWidth() - 40, 6, 32, 16));
        r.removeFromTop (58);
        r.removeFromBottom (24);
        r.removeFromRight (16);
        knob.setBounds (r);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        const auto& info = bm::params::kStages[stage];
        const auto rf = getLocalBounds().toFloat().reduced (2.0f, 0.0f);
        const bool on = isOn();

        g.setColour (p.panelRaised.withAlpha (on ? 1.0f : 0.6f));
        g.fillRoundedRectangle (rf, 8.0f);
        g.setColour (selected ? p.accent : p.border);
        g.drawRoundedRectangle (rf.reduced (0.5f), 8.0f, selected ? 1.5f : 1.0f);

        const float a = on ? 1.0f : 0.45f;
        g.setColour (p.textDim.withAlpha (a));
        g.setFont (monoFont (10.0f, true));
        g.drawText (juce::String (stage + 1).paddedLeft ('0', 2), rf.reduced (8.0f, 6.0f).withHeight (12.0f), juce::Justification::centredLeft);

        g.setColour (p.text.withAlpha (a));
        g.setFont (font (11.5f, true));
        g.drawFittedText (info.name, getLocalBounds().reduced (8, 0).withTrimmedTop (24).withHeight (24), juce::Justification::centred, 2, 0.85f);

        g.setColour (p.textDim.withAlpha (a));
        g.setFont (font (10.0f));
        g.drawText (on ? info.gainLabel : juce::String ("off"), getLocalBounds().withTrimmedRight (16).withTrimmedTop (46).withHeight (12), juce::Justification::centredTop);

        // mini meter: RMS fill with a peak tick, -60 to 0 dBFS
        auto m = juce::Rectangle<float> (rf.getRight() - 16.0f, rf.getY() + 48.0f, 8.0f, rf.getHeight() - 48.0f - 26.0f);
        g.setColour (p.border.withAlpha (0.6f));
        g.fillRoundedRectangle (m, 2.0f);
        auto yFor = [&] (float db) { return m.getBottom() - m.getHeight() * (juce::jlimit (-60.0f, 0.0f, db) + 60.0f) / 60.0f; };
        const float yr = yFor (rmsShown);
        g.setColour ((peakHold > -1.0f ? p.warn : p.good).withAlpha (0.9f));
        g.fillRect (juce::Rectangle<float> (m.getX(), yr, m.getWidth(), m.getBottom() - yr));
        if (peakHold > -119.0f)
        {
            g.setColour (peakHold > -0.1f ? p.bad : p.text);
            g.fillRect (juce::Rectangle<float> (m.getX() - 1.0f, yFor (peakHold) - 1.0f, m.getWidth() + 2.0f, 2.0f));
        }
    }

private:
    int stage;
    BroadcastMasterProcessor& proc;
    std::function<void (int)> onSelect;
    juce::Slider knob;
    juce::TextButton onButton { "ON" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttach;
    std::atomic<float>* enableParam = nullptr;
    bool selected = false;
    float peakHold = -120.0f, rmsShown = -120.0f;
};

class ChainStrip : public juce::Component
{
public:
    std::function<void (int)> onSelect;

    explicit ChainStrip (BroadcastMasterProcessor& p)
    {
        for (int i = 0; i < bm::params::kNumStages; ++i)
        {
            tiles[(size_t) i] = std::make_unique<StageTile> (i, p, [this] (int s) { if (onSelect) onSelect (s); });
            addAndMakeVisible (*tiles[(size_t) i]);
        }
    }

    void setSelected (int stage) { for (int i = 0; i < bm::params::kNumStages; ++i) tiles[(size_t) i]->setSelected (i == stage); }
    void tick() { for (auto& t : tiles) t->tick(); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 8);
        r.removeFromTop (14);
        const int n = bm::params::kNumStages;
        const float w = (float) r.getWidth() / (float) n;
        for (int i = 0; i < n; ++i)
            tiles[(size_t) i]->setBounds (juce::Rectangle<int> ((int) std::round (r.getX() + w * (float) i), r.getY(),
                                                                (int) std::round (w), r.getHeight()));
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, getLocalBounds().toFloat(), "Signal chain  -  click a stage for its settings   |   output gain after every stage");
    }

private:
    std::array<std::unique_ptr<StageTile>, bm::params::kNumStages> tiles;
};
} // namespace bm::ui
