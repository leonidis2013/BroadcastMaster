#pragma once
#include <deque>
#include "Theme.h"
#include "UiState.h"
#include "../PluginProcessor.h"

namespace bm::ui
{
// Dynamics readouts (crest factor, PSR, PLR, loudness range, sample peak) and a gain-reduction history graph.
class DynamicsStrip : public juce::Component
{
public:
    explicit DynamicsStrip (BroadcastMasterProcessor& p) : proc (p) {}

    void tick (const UiState& st)
    {
        state = st;
        grComp.push_back (proc.gainReductionComp.load());
        grLim.push_back (proc.gainReductionLimiter.load());
        while (grComp.size() > kHistLen) grComp.pop_front();
        while (grLim.size() > kHistLen) grLim.pop_front();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, getLocalBounds().toFloat(), {});
        const auto& s = state.shown();

        auto r = getLocalBounds().reduced (8, 8);
        auto graph = r.removeFromRight (juce::jmin (360, r.getWidth() / 3));
        r.removeFromRight (10);

        const int cell = r.getWidth() / 5;
        cellDraw (g, r.removeFromLeft (cell), "CREST", s.crest > 0.0f ? juce::String (s.crest, 1) : "--", "dB",
                  state.out.crest - state.in.crest, s.crest > 0.0f && state.in.crest > 0.0f && state.out.crest > 0.0f);
        cellDraw (g, r.removeFromLeft (cell), "PSR", s.hasPsr ? juce::String (s.psr, 1) : "--", "dB",
                  state.out.psr - state.in.psr, state.in.hasPsr && state.out.hasPsr);
        cellDraw (g, r.removeFromLeft (cell), "PLR", s.hasPlr ? juce::String (s.plr, 1) : "--", "dB",
                  state.out.plr - state.in.plr, state.in.hasPlr && state.out.hasPlr);
        cellDraw (g, r.removeFromLeft (cell), "LOUDNESS RANGE", s.lra > 0.0f ? juce::String (s.lra, 1) : "--", "LU",
                  state.out.lra - state.in.lra, true);
        cellDraw (g, r, "SAMPLE PEAK", dbText (s.samplePeak), "dBFS",
                  state.out.samplePeak - state.in.samplePeak, state.in.samplePeak > -119.0f && state.out.samplePeak > -119.0f);

        paintGr (g, graph.toFloat());
    }

private:
    static constexpr size_t kHistLen = 300; // 10 s at 30 Hz

    void cellDraw (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& name, const juce::String& value,
                   const juce::String& unit, float delta, bool deltaValid)
    {
        const auto& p = pal();
        const auto rf = r.toFloat().reduced (3.0f, 0.0f);
        g.setColour (p.panelRaised);
        g.fillRoundedRectangle (rf, 6.0f);
        g.setColour (p.textDim);
        g.setFont (font (10.0f, true));
        g.drawText (name, rf.reduced (8.0f, 4.0f).withHeight (12.0f), juce::Justification::centredLeft);
        g.setColour (p.text);
        g.setFont (monoFont (20.0f, true));
        g.drawText (value, rf.reduced (8.0f, 0.0f).withTrimmedTop (12.0f), juce::Justification::centredLeft);
        g.setColour (p.textDim);
        g.setFont (font (10.0f));
        g.drawText (unit, rf.reduced (8.0f, 4.0f), juce::Justification::topRight);
        if (state.compare && deltaValid)
        {
            g.setColour (p.accent);
            g.setFont (monoFont (10.0f, true));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xce\x94 ")) + signedText (delta), rf.reduced (8.0f, 4.0f), juce::Justification::bottomRight);
        }
    }

    void paintGr (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto& p = pal();
        g.setColour (p.bg.withAlpha (p.dark ? 0.55f : 0.5f));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (p.textDim);
        g.setFont (font (10.0f, true));
        g.drawText ("GAIN REDUCTION  10 s", r.reduced (8.0f, 4.0f).withHeight (12.0f), juce::Justification::centredLeft);

        const auto plot = r.reduced (8.0f, 4.0f).withTrimmedTop (16.0f);
        const float maxGr = 12.0f;
        g.setColour (p.grid);
        for (float db = 0.0f; db <= maxGr; db += 3.0f)
            g.drawHorizontalLine ((int) (plot.getY() + plot.getHeight() * db / maxGr), plot.getX(), plot.getRight());

        auto draw = [&] (const std::deque<float>& h, juce::Colour c)
        {
            if (h.size() < 2) return;
            juce::Path path;
            path.startNewSubPath (plot.getRight() - plot.getWidth() * (float) (h.size() - 1) / (float) kHistLen, plot.getY());
            for (size_t i = 0; i < h.size(); ++i)
            {
                const float x = plot.getRight() - plot.getWidth() * (float) (h.size() - 1 - i) / (float) kHistLen;
                path.lineTo (x, plot.getY() + plot.getHeight() * juce::jlimit (0.0f, maxGr, h[i]) / maxGr);
            }
            path.lineTo (plot.getRight(), plot.getY());
            path.closeSubPath();
            g.setColour (c.withAlpha (0.35f));
            g.fillPath (path);
            g.setColour (c);
            g.strokePath (path, juce::PathStrokeType (1.2f));
        };
        draw (grComp, p.warn);
        draw (grLim, p.bad);

        g.setColour (p.warn);
        g.drawText ("comp", plot.withHeight (12.0f), juce::Justification::topRight);
        g.setColour (p.bad);
        g.drawText ("limiter", plot.withHeight (12.0f).translated (-34.0f, 0.0f), juce::Justification::topRight);
    }

    BroadcastMasterProcessor& proc;
    UiState state;
    std::deque<float> grComp, grLim;
};
} // namespace bm::ui
