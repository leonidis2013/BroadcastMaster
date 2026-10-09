#pragma once
#include "Theme.h"
#include "UiState.h"
#include "../PluginProcessor.h"

namespace bm::ui
{
// Stereo image: goniometer, correlation, balance, mid/side levels and mono-compatibility loss.
class StereoPanel : public juce::Component
{
public:
    explicit StereoPanel (BroadcastMasterProcessor& p) : proc (p) {}

    void tick (const UiState& st)
    {
        state = st;
        bm::MeterBank& bank = st.showOutput ? proc.metersOut : proc.metersIn;
        bank.stereo.copyScope (scopeL, scopeR, kScopePoints);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, getLocalBounds().toFloat(), "Stereo image");
        const auto& p = pal();
        const auto& s = state.shown();

        auto r = getLocalBounds().reduced (10, 0);
        r.removeFromTop (26);
        r.removeFromBottom (10);

        const int side = juce::jmin (r.getHeight(), 190);
        paintGoniometer (g, r.removeFromLeft (side).withHeight (side).toFloat());
        r.removeFromLeft (14);

        const int rowH = r.getHeight() / 5;

        // Correlation
        {
            auto row = r.removeFromTop (rowH);
            label (g, row, "CORRELATION");
            auto bar = row.withTrimmedTop (14).withTrimmedRight (74).toFloat().reduced (0.0f, 3.0f);
            paintCenteredBar (g, bar, s.correlation, -1.0f, 1.0f,
                              s.correlation < 0.0f ? p.bad : (s.correlation < 0.3f ? p.warn : p.good));
            valueText (g, row.withTrimmedTop (10), juce::String (s.correlation, 2), compareText (state.out.correlation - state.in.correlation, 2));
        }

        // Balance
        {
            auto row = r.removeFromTop (rowH);
            label (g, row, "BALANCE  L / R");
            auto bar = row.withTrimmedTop (14).withTrimmedRight (74).toFloat().reduced (0.0f, 3.0f);
            const float bal = s.balanceDb;
            paintCenteredBar (g, bar, bal, -12.0f, 12.0f, std::fabs (bal) < 1.5f ? p.good : (std::fabs (bal) < 3.0f ? p.warn : p.bad));
            const juce::String t = std::fabs (bal) < 0.1f ? "C" : (bal < 0.0f ? "L " : "R ") + juce::String (std::fabs (bal), 1);
            valueText (g, row.withTrimmedTop (10), t, {});
        }

        // Mid / Side
        {
            auto row = r.removeFromTop (rowH * 2);
            label (g, row, "MID / SIDE  (RMS bar, peak tick)");
            auto bars = row.withTrimmedTop (14).withTrimmedRight (74);
            const int h = bars.getHeight() / 2;
            paintLevelBar (g, bars.removeFromTop (h).toFloat().reduced (0.0f, 3.0f), s.midRms, s.midPeak, "M");
            paintLevelBar (g, bars.toFloat().reduced (0.0f, 3.0f), s.sideRms, s.sidePeak, "S");
            auto vals = row.withTrimmedTop (14).removeFromRight (70);
            g.setColour (p.text);
            g.setFont (monoFont (12.0f, true));
            g.drawText (dbText (s.midRms), vals.removeFromTop (vals.getHeight() / 2), juce::Justification::centredRight);
            g.drawText (dbText (s.sideRms), vals, juce::Justification::centredRight);
        }

        // Mono compatibility
        {
            auto row = r;
            label (g, row, "MONO SUM LOSS");
            const float loss = s.monoLossDb;
            const auto col = loss > -3.5f ? p.good : (loss > -6.0f ? p.warn : p.bad);
            g.setColour (col);
            g.setFont (monoFont (14.0f, true));
            g.drawText (juce::String (loss, 1) + " dB", row.withTrimmedTop (12).withTrimmedRight (74), juce::Justification::centredLeft);
            valueText (g, row.withTrimmedTop (10), {}, compareText (state.out.monoLossDb - state.in.monoLossDb, 1));
        }
    }

private:
    static constexpr int kScopePoints = 1024;

    juce::String compareText (float d, int decimals) const
    {
        return state.compare ? juce::String (juce::CharPointer_UTF8 ("\xce\x94 ")) + signedText (d, decimals) : juce::String();
    }

    void label (juce::Graphics& g, juce::Rectangle<int> row, const juce::String& t)
    {
        g.setColour (pal().textDim);
        g.setFont (font (10.0f, true));
        g.drawText (t, row.withHeight (14), juce::Justification::centredLeft);
    }

    void valueText (juce::Graphics& g, juce::Rectangle<int> row, const juce::String& v, const juce::String& delta)
    {
        auto area = row.removeFromRight (70);
        if (v.isNotEmpty())
        {
            g.setColour (pal().text);
            g.setFont (monoFont (13.0f, true));
            g.drawText (v, area, juce::Justification::centredRight);
        }
        if (delta.isNotEmpty())
        {
            g.setColour (pal().accent);
            g.setFont (monoFont (10.0f, true));
            g.drawText (delta, area, juce::Justification::bottomRight);
        }
    }

    void paintCenteredBar (juce::Graphics& g, juce::Rectangle<float> bar, float v, float lo, float hi, juce::Colour col)
    {
        const auto& p = pal();
        g.setColour (p.panelRaised);
        g.fillRoundedRectangle (bar, 3.0f);
        const float cx = bar.getCentreX();
        const float x = bar.getX() + bar.getWidth() * (juce::jlimit (lo, hi, v) - lo) / (hi - lo);
        g.setColour (col.withAlpha (0.85f));
        g.fillRect (juce::Rectangle<float> (juce::jmin (cx, x), bar.getY(), std::fabs (x - cx), bar.getHeight()));
        g.setColour (p.textDim);
        g.drawVerticalLine ((int) cx, bar.getY(), bar.getBottom());
        g.setColour (p.text);
        g.fillRect (juce::Rectangle<float> (x - 1.5f, bar.getY() - 2.0f, 3.0f, bar.getHeight() + 4.0f));
    }

    void paintLevelBar (juce::Graphics& g, juce::Rectangle<float> bar, float rms, float peak, const juce::String& name)
    {
        const auto& p = pal();
        auto body = bar.withTrimmedLeft (16.0f);
        g.setColour (p.textDim);
        g.setFont (font (10.0f, true));
        g.drawText (name, bar.withWidth (14.0f), juce::Justification::centredLeft);
        g.setColour (p.panelRaised);
        g.fillRoundedRectangle (body, 3.0f);

        auto xFor = [&] (float db) { return body.getX() + body.getWidth() * (juce::jlimit (-60.0f, 0.0f, db) + 60.0f) / 60.0f; };
        const auto col = rms > -6.0f ? p.warn : p.good;
        g.setColour (col.withAlpha (0.85f));
        g.fillRect (juce::Rectangle<float> (body.getX(), body.getY(), xFor (rms) - body.getX(), body.getHeight()));
        if (peak > -119.0f)
        {
            g.setColour (p.text);
            g.fillRect (juce::Rectangle<float> (xFor (peak) - 1.0f, body.getY(), 2.0f, body.getHeight()));
        }
    }

    void paintGoniometer (juce::Graphics& g, juce::Rectangle<float> area)
    {
        const auto& p = pal();
        g.setColour (p.bg.withAlpha (p.dark ? 0.55f : 0.5f));
        g.fillRoundedRectangle (area, 6.0f);

        const auto c = area.getCentre();
        const float rad = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 14.0f;

        g.setColour (p.grid);
        g.drawEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f, 1.0f);
        g.drawEllipse (c.x - rad * 0.5f, c.y - rad * 0.5f, rad, rad, 1.0f);
        g.drawLine (c.x, c.y - rad, c.x, c.y + rad);
        g.drawLine (c.x - rad, c.y, c.x + rad, c.y);
        g.drawLine (c.x - rad * 0.707f, c.y - rad * 0.707f, c.x + rad * 0.707f, c.y + rad * 0.707f);
        g.drawLine (c.x - rad * 0.707f, c.y + rad * 0.707f, c.x + rad * 0.707f, c.y - rad * 0.707f);

        g.setColour (p.textDim);
        g.setFont (font (10.0f, true));
        g.drawText ("M", juce::Rectangle<float> (c.x - 10.0f, area.getY() + 1.0f, 20.0f, 12.0f), juce::Justification::centred);
        g.drawText ("S", juce::Rectangle<float> (area.getRight() - 14.0f, c.y - 6.0f, 12.0f, 12.0f), juce::Justification::centred);
        g.drawText ("L", juce::Rectangle<float> (area.getX() + 3.0f, area.getY() + 3.0f, 12.0f, 12.0f), juce::Justification::centred);
        g.drawText ("R", juce::Rectangle<float> (area.getRight() - 15.0f, area.getY() + 3.0f, 12.0f, 12.0f), juce::Justification::centred);

        const auto col = state.showOutput ? p.outputCol : p.inputCol;
        const float k = rad * 0.7f * 0.70710678f * 1.4142136f; // full-scale mono reaches the top of the ring
        g.saveState();
        g.reduceClipRegion (area.toNearestInt());
        for (int i = 0; i < kScopePoints; ++i)
        {
            const float l = scopeL[i], r = scopeR[i];
            const float mid = (l + r) * 0.70710678f, sid = (l - r) * 0.70710678f;
            const float x = c.x + sid * k * 1.0f, y = c.y - mid * k * 1.0f;
            const float age = (float) i / (float) kScopePoints; // newest points are brightest
            g.setColour (col.withAlpha (0.12f + 0.55f * age));
            g.fillRect (x - 0.75f, y - 0.75f, 1.5f, 1.5f);
        }
        g.restoreState();
    }

    BroadcastMasterProcessor& proc;
    UiState state;
    float scopeL[kScopePoints] {}, scopeR[kScopePoints] {};
};
} // namespace bm::ui
