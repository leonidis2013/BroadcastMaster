#pragma once
#include <deque>
#include "Theme.h"
#include "UiState.h"
#include "../Parameters.h"

namespace bm::ui
{
// Loudness readouts (momentary, short-term, integrated, LRA, true peak) judged against the selected
// destination preset, plus a 60 second momentary/short-term trend graph.
class LoudnessPanel : public juce::Component
{
public:
    explicit LoudnessPanel (juce::AudioProcessorValueTreeState& apvts, std::function<void()> onReset)
        : resetCallback (std::move (onReset))
    {
        presetBox.addItemList (bm::params::presetNames(), 1);
        presetAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, bm::params::loudPreset, presetBox);
        addAndMakeVisible (presetBox);

        resetButton.setButtonText ("Reset");
        resetButton.setTooltip ("Restart integrated loudness, loudness range and peak holds");
        resetButton.onClick = [this] { if (resetCallback) resetCallback(); histM.clear(); histS.clear(); };
        addAndMakeVisible (resetButton);
    }

    void tick (const UiState& st)
    {
        state = st;
        if (++divider >= 3) // trend history at 10 Hz
        {
            divider = 0;
            const auto& s = st.shown();
            histM.push_back (s.momentary);
            histS.push_back (s.shortTerm);
            while (histM.size() > kHistLen) histM.pop_front();
            while (histS.size() > kHistLen) histS.pop_front();
        }
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 0);
        auto top = r.removeFromTop (28).withTrimmedTop (4);
        resetButton.setBounds (top.removeFromRight (56));
        top.removeFromRight (6);
        presetBox.setBounds (top.removeFromRight (176));
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, getLocalBounds().toFloat(), "Loudness");
        const auto& p = pal();
        const auto& s = state.shown();

        auto r = getLocalBounds().reduced (10, 0);
        r.removeFromTop (32);
        r.removeFromBottom (10);
        auto left = r.removeFromLeft (156);
        r.removeFromLeft (10);

        auto intCol = colourFor (s.integrated, state.target, state.tolerance);
        auto stCol  = colourFor (s.shortTerm, state.target, state.tolerance * 2.0f);

        const float dInt = state.out.integrated - state.in.integrated;
        const float dSt  = state.out.shortTerm - state.in.shortTerm;
        const float dMom = state.out.momentary - state.in.momentary;
        const float dLra = state.out.lra - state.in.lra;
        const float dTp  = state.out.tpHold - state.in.tpHold;

        drawReadout (g, left.removeFromTop (74), "INTEGRATED", dbText (s.integrated), "LUFS", intCol, 32.0f,
                     deltaText (dInt, "LU", state.in.integrated, state.out.integrated));
        left.removeFromTop (4);
        drawReadout (g, left.removeFromTop (48), "SHORT-TERM", dbText (s.shortTerm), "LUFS", stCol, 22.0f,
                     deltaText (dSt, "LU", state.in.shortTerm, state.out.shortTerm));
        left.removeFromTop (4);
        drawReadout (g, left.removeFromTop (48), "MOMENTARY", dbText (s.momentary), "LUFS", p.text, 22.0f,
                     deltaText (dMom, "LU", state.in.momentary, state.out.momentary));
        left.removeFromTop (4);

        auto row = left.removeFromTop (48);
        const float tpState = s.tpHold - state.ceiling;
        const auto tpCol = s.tpHold <= -119.0f ? p.textDim : (tpState > 0.0f ? p.bad : (tpState > -1.0f ? p.warn : p.good));
        drawReadout (g, row.removeFromLeft (row.getWidth() / 2 - 2), "LRA", juce::String (s.lra, 1), "LU", p.text, 18.0f,
                     deltaText (dLra, "LU", 0.0f, 0.0f, true));
        row.removeFromLeft (4);
        drawReadout (g, row, "TRUE PEAK", dbText (s.tpHold), "dBTP", tpCol, 18.0f,
                     deltaText (dTp, "dB", state.in.tpHold, state.out.tpHold));

        paintTrend (g, r.toFloat());
    }

private:
    static constexpr size_t kHistLen = 600;

    static juce::Colour colourFor (float v, float target, float tol)
    {
        if (v <= -119.0f) return pal().textDim;
        const float d = std::fabs (v - target);
        return d <= tol ? pal().good : (d <= tol * 2.0f ? pal().warn : pal().bad);
    }

    juce::String deltaText (float d, const char* unit, float inV, float outV, bool force = false) const
    {
        if (! state.compare) return {};
        if (! force && (inV <= -119.0f || outV <= -119.0f)) return {};
        return juce::String (juce::CharPointer_UTF8 ("\xce\x94 ")) + signedText (d) + " " + unit;
    }

    void drawReadout (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& label, const juce::String& value,
                      const juce::String& unit, juce::Colour col, float valueSize, const juce::String& delta)
    {
        const auto& p = pal();
        const auto rf = r.toFloat();
        g.setColour (p.panelRaised);
        g.fillRoundedRectangle (rf, 6.0f);

        g.setColour (p.textDim);
        g.setFont (font (10.0f, true));
        g.drawText (label, rf.reduced (8.0f, 4.0f).withHeight (12.0f), juce::Justification::centredLeft);

        g.setColour (col);
        g.setFont (monoFont (valueSize, true));
        auto vr = rf.reduced (8.0f, 0.0f).withTrimmedTop (14.0f);
        g.drawText (value, vr, juce::Justification::centredLeft);

        g.setColour (p.textDim);
        g.setFont (font (10.0f));
        g.drawText (unit, rf.reduced (8.0f, 4.0f), juce::Justification::topRight);

        if (delta.isNotEmpty())
        {
            g.setColour (p.accent);
            g.setFont (monoFont (10.0f, true));
            g.drawText (delta, rf.reduced (8.0f, 4.0f), juce::Justification::bottomRight);
        }
    }

    void paintTrend (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto& p = pal();
        g.setColour (p.bg.withAlpha (p.dark ? 0.55f : 0.5f));
        g.fillRoundedRectangle (r, 6.0f);

        const auto plot = r.reduced (30.0f, 8.0f).withTrimmedBottom (8.0f);
        const float top = state.target + 9.0f, bottom = state.target - 21.0f;
        auto yFor = [&] (float v) { return plot.getY() + plot.getHeight() * (1.0f - (juce::jlimit (bottom, top, v) - bottom) / (top - bottom)); };

        // tolerance band and target line
        g.setColour (p.good.withAlpha (0.14f));
        g.fillRect (juce::Rectangle<float> (plot.getX(), yFor (state.target + state.tolerance), plot.getWidth(),
                                            yFor (state.target - state.tolerance) - yFor (state.target + state.tolerance)));

        g.setFont (font (10.0f));
        for (float v = std::ceil (bottom / 6.0f) * 6.0f; v <= top; v += 6.0f)
        {
            g.setColour (p.grid);
            g.drawHorizontalLine ((int) yFor (v), plot.getX(), plot.getRight());
            g.setColour (p.textDim);
            g.drawText (juce::String ((int) v), juce::Rectangle<float> (r.getX() + 2.0f, yFor (v) - 6.0f, 26.0f, 12.0f), juce::Justification::centredRight);
        }

        g.setColour (p.good);
        g.drawHorizontalLine ((int) yFor (state.target), plot.getX(), plot.getRight());
        g.setFont (font (10.0f, true));
        g.drawText (juce::String (state.target, 0) + " LUFS", plot.withHeight (14.0f).translated (0.0f, yFor (state.target) - plot.getY() - 14.0f),
                    juce::Justification::centredRight);

        auto draw = [&] (const std::deque<float>& h, juce::Colour c, float thickness)
        {
            if (h.size() < 2) return;
            juce::Path path;
            bool started = false;
            for (size_t i = 0; i < h.size(); ++i)
            {
                if (h[i] <= -119.0f) { started = false; continue; }
                const float x = plot.getRight() - plot.getWidth() * (float) (h.size() - 1 - i) / (float) kHistLen;
                const float y = yFor (h[i]);
                if (! started) { path.startNewSubPath (x, y); started = true; } else path.lineTo (x, y);
            }
            g.setColour (c);
            g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::curved));
        };

        g.saveState();
        g.reduceClipRegion (plot.toNearestInt());
        draw (histM, p.textDim.withAlpha (0.8f), 1.0f);
        draw (histS, state.showOutput ? p.outputCol : p.inputCol, 2.0f);
        g.restoreState();

        g.setColour (p.textDim);
        g.setFont (font (10.0f));
        g.drawText ("60 s", plot.withHeight (12.0f).translated (0.0f, plot.getHeight() - 12.0f), juce::Justification::bottomLeft);
        g.drawText ("now", plot.withHeight (12.0f).translated (0.0f, plot.getHeight() - 12.0f), juce::Justification::bottomRight);
        g.drawText ("momentary (grey)  short-term (colour)", plot.withHeight (12.0f), juce::Justification::topLeft);
    }

    std::function<void()> resetCallback;
    juce::ComboBox presetBox;
    juce::TextButton resetButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> presetAttach;
    UiState state;
    std::deque<float> histM, histS;
    int divider = 0;
};
} // namespace bm::ui
