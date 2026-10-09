#pragma once
#include <memory>
#include "Theme.h"
#include "../PluginProcessor.h"

namespace bm::ui
{
inline void setParamPlain (juce::AudioProcessorValueTreeState& apvts, const char* id, float value)
{
    if (auto* prm = apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
        prm->endChangeGesture();
    }
}

// One parameter shown as a labelled control, built from its ParamSpec (knob, bar, switch or drop-down).
class ParamControl : public juce::Component
{
public:
    enum class Style { Knob, Bar };

    ParamControl (juce::AudioProcessorValueTreeState& apvts, const bm::params::ParamSpec& spec, Style st,
                  juce::String labelText = {}, int barLabelWidth = 56)
        : style (st), labelWidth (barLabelWidth), kind (spec.kind)
    {
        label = labelText.isEmpty() ? juce::String (spec.name) : labelText;
        const juce::String tip = juce::String (spec.name) + (spec.unit.empty() ? "" : " (" + juce::String (spec.unit) + ")");

        switch (spec.kind)
        {
            case bm::params::Kind::Bool:
                toggle.setButtonText (label);
                toggle.setTooltip (tip);
                addAndMakeVisible (toggle);
                buttonAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, spec.id, toggle);
                break;

            case bm::params::Kind::Choice:
                combo.addItemList (spec.choices, 1);
                combo.setTooltip (tip);
                addAndMakeVisible (combo);
                comboAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, spec.id, combo);
                break;

            default:
            {
                if (style == Style::Knob)
                {
                    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
                    slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
                }
                else
                {
                    slider.setSliderStyle (juce::Slider::LinearHorizontal);
                    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 66, 20);
                }
                const juce::String unit (spec.unit);
                if (unit == "Hz")
                {
                    slider.textFromValueFunction = [] (double v)
                    {
                        return v >= 1000.0 ? juce::String (v / 1000.0, 2) + " kHz" : juce::String (v, v < 100.0 ? 1 : 0) + " Hz";
                    };
                    slider.valueFromTextFunction = [] (const juce::String& t)
                    {
                        const auto s = t.trim().toLowerCase();
                        double v = s.getDoubleValue();
                        if (s.contains ("k")) v *= 1000.0;
                        return v;
                    };
                }
                else
                {
                    const int decimals = spec.step >= 1.0f ? 0 : (spec.step >= 0.1f ? 1 : 2);
                    slider.setNumDecimalPlacesToDisplay (decimals);
                    slider.setTextValueSuffix (unit.isEmpty() ? "" : " " + unit);
                }
                slider.setDoubleClickReturnValue (true, spec.def);
                slider.setTooltip (tip + ". Double-click to reset.");
                addAndMakeVisible (slider);
                sliderAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, spec.id, slider);
                break;
            }
        }
    }

    static int preferredWidth (const bm::params::ParamSpec& s, Style st)
    {
        if (s.kind == bm::params::Kind::Bool) return 130;
        if (s.kind == bm::params::Kind::Choice) return 140;
        return st == Style::Knob ? 92 : 220;
    }

    static int preferredHeight (const bm::params::ParamSpec& s, Style st)
    {
        if (s.kind == bm::params::Kind::Bool) return 28;
        if (s.kind == bm::params::Kind::Choice) return st == Style::Knob ? 52 : 28;
        return st == Style::Knob ? 108 : 28;
    }

    void paint (juce::Graphics& g) override
    {
        if (kind == bm::params::Kind::Bool) return;
        g.setColour (pal().textDim);
        g.setFont (font (11.0f));
        if (style == Style::Knob)
            g.drawText (label, getLocalBounds().withHeight (14), juce::Justification::centred);
        else
            g.drawText (label, getLocalBounds().withWidth (labelWidth), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        if (kind == bm::params::Kind::Bool)
            toggle.setBounds (r);
        else if (style == Style::Knob)
        {
            r.removeFromTop (14);
            if (kind == bm::params::Kind::Choice) combo.setBounds (r.removeFromTop (28)); else slider.setBounds (r);
        }
        else
        {
            r.removeFromLeft (labelWidth);
            if (kind == bm::params::Kind::Choice) combo.setBounds (r.withHeight (26)); else slider.setBounds (r);
        }
    }

private:
    Style style;
    int labelWidth;
    bm::params::Kind kind;
    juce::String label;
    juce::Slider slider;
    juce::ToggleButton toggle;
    juce::ComboBox combo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttach;
};

// A stage on/off switch bound to its enable parameter.
class StageEnableToggle : public juce::ToggleButton
{
public:
    StageEnableToggle (juce::AudioProcessorValueTreeState& apvts, const char* paramId, const juce::String& text = "Enabled")
        : juce::ToggleButton (text)
    {
        attach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, paramId, *this);
        setTooltip ("Switch this stage on or off. Switching never changes the plug-in's latency.");
    }

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attach;
};
} // namespace bm::ui
