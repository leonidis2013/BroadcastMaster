#pragma once
#include "Theme.h"
#include "UiState.h"
#include "../PluginProcessor.h"

namespace bm::ui
{
// Real-time analyser (top) and scrolling spectrogram (bottom). The curve and spectrogram follow the
// Input/Output switch; with Compare on, the other signal is overlaid as an outline.
class SpectrumPanel : public juce::Component
{
public:
    explicit SpectrumPanel (BroadcastMasterProcessor& p) : proc (p) {}

    void setSettings (bm::Weighting w, int targetMode, int bandsPerOctave)
    {
        weighting = w;
        targetSlope = targetMode == 1 ? 3.0f : (targetMode == 2 ? 4.5f : 0.0f);
        bpo = bandsPerOctave;
    }

    // Draws the tonal EQ curve (mid and side) over the analyser while the EQ stage is selected.
    void setEqOverlay (bool show, const bm::dsp::EqParams& params)
    {
        showEq = show;
        eqParams = params;
    }

    void tick (const UiState& st)
    {
        const double fs = proc.getCurrentSampleRate();
        if (fs != lastFs || bpo != lastBpo)
        {
            lastFs = fs; lastBpo = bpo;
            analyzer.prepare (fs);
            analyzer.setBandsPerOctave (bpo);
            smoothShown.assign ((size_t) analyzer.numBands(), kFloorDb);
            smoothOther.assign ((size_t) analyzer.numBands(), kFloorDb);
        }

        bm::MeterBank& shownBank = st.showOutput ? proc.metersOut : proc.metersIn;
        bm::MeterBank& otherBank = st.showOutput ? proc.metersIn : proc.metersOut;

        analyzer.analyse (shownBank.ringL, shownBank.ringR, weighting, raw);
        smooth (smoothShown, raw);
        showOther = st.compare;
        showingOutput = st.showOutput;

        if (showOther)
        {
            analyzer.analyse (otherBank.ringL, otherBank.ringR, weighting, raw);
            smooth (smoothOther, raw);
        }

        updateTargetOffset();
        pushSpectrogramColumn();
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().toFloat().reduced (10.0f);
        r.removeFromTop (18.0f);
        const float rtaH = r.getHeight() * 0.62f;
        rtaArea = r.removeFromTop (rtaH).toNearestInt();
        r.removeFromTop (6.0f);
        specArea = r.toNearestInt().withTrimmedBottom (14);

        const int w = juce::jmax (8, specArea.getWidth() - 34), h = juce::jmax (8, specArea.getHeight());
        specImage = juce::Image (juce::Image::ARGB, w, h, true);
        specCol = 0;
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, getLocalBounds().toFloat(), "Spectrum");

        g.setColour (pal().textDim);
        g.setFont (font (11.0f));
        const juce::String srcName = showingOutput ? "OUTPUT" : "INPUT";
        g.drawText (srcName, getLocalBounds().toFloat().reduced (12.0f).withHeight (18.0f), juce::Justification::centredRight);

        paintRta (g);
        paintSpectrogram (g);
    }

private:
    static constexpr float kFloorDb = -120.0f, kTop = -10.0f, kBottom = -110.0f;

    float xForFreq (double f, juce::Rectangle<float> r) const
    {
        return r.getX() + r.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0));
    }

    float yForDb (float db, juce::Rectangle<float> r) const
    {
        return r.getY() + r.getHeight() * (1.0f - (juce::jlimit (kBottom, kTop, db) - kBottom) / (kTop - kBottom));
    }

    void smooth (std::vector<float>& sm, const std::vector<float>& in)
    {
        if (sm.size() != in.size()) sm.assign (in.size(), kFloorDb);
        for (size_t i = 0; i < in.size(); ++i)
            sm[i] += (in[i] - sm[i]) * (in[i] > sm[i] ? 0.7f : 0.15f);
    }

    float targetShape (double f) const { return -targetSlope * (float) std::log2 (f / 1000.0); }

    // Align the target curve to the measured level between 200 Hz and 5 kHz so the overlay shows tilt, not level.
    void updateTargetOffset()
    {
        if (targetSlope <= 0.0f) return;
        double sum = 0.0; int cnt = 0;
        for (int i = 0; i < analyzer.numBands(); ++i)
        {
            const double f = analyzer.bandCentre (i);
            if (f < 200.0 || f > 5000.0 || smoothShown[(size_t) i] < -105.0f) continue;
            sum += smoothShown[(size_t) i] - targetShape (f);
            ++cnt;
        }
        if (cnt > 0) targetOffset += ((float) (sum / cnt) - targetOffset) * 0.05f;
    }

    juce::Path makeCurve (const std::vector<float>& v, juce::Rectangle<float> r, bool closed) const
    {
        juce::Path path;
        const int n = analyzer.numBands();
        for (int i = 0; i < n; ++i)
        {
            const float x = xForFreq (analyzer.bandCentre (i), r), y = yForDb (v[(size_t) i], r);
            if (i == 0) { if (closed) { path.startNewSubPath (x, r.getBottom()); path.lineTo (x, y); } else path.startNewSubPath (x, y); }
            else path.lineTo (x, y);
        }
        if (closed && n > 0) { path.lineTo (xForFreq (analyzer.bandCentre (n - 1), r), r.getBottom()); path.closeSubPath(); }
        return path;
    }

    void paintRta (juce::Graphics& g)
    {
        const auto& p = pal();
        const auto full = rtaArea.toFloat();
        auto r = full.withTrimmedLeft (34.0f).withTrimmedBottom (16.0f);

        g.setColour (p.bg.withAlpha (p.dark ? 0.55f : 0.5f));
        g.fillRoundedRectangle (r, 4.0f);

        g.setFont (font (10.0f));
        for (float db = kBottom; db <= kTop + 0.1f; db += 10.0f)
        {
            const float y = yForDb (db, r);
            g.setColour (p.grid);
            g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
            g.setColour (p.textDim);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (full.getX(), y - 6.0f, 30.0f, 12.0f), juce::Justification::centredRight);
        }

        const double freqs[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
        for (double f : freqs)
        {
            const float x = xForFreq (f, r);
            g.setColour (p.grid);
            g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
            g.setColour (p.textDim);
            const juce::String t = f >= 1000 ? juce::String (f / 1000.0, f == 1000 || f == 2000 || f == 5000 || f >= 10000 ? 0 : 1) + "k" : juce::String ((int) f);
            g.drawText (t, juce::Rectangle<float> (x - 20.0f, r.getBottom() + 2.0f, 40.0f, 12.0f), juce::Justification::centred);
        }

        if (analyzer.numBands() < 2 || smoothShown.size() != (size_t) analyzer.numBands()) return;

        g.saveState();
        g.reduceClipRegion (r.toNearestInt());

        const auto shownCol = showingOutput ? p.outputCol : p.inputCol;
        const auto otherCol = showingOutput ? p.inputCol : p.outputCol;

        if (showOther)
        {
            g.setColour (otherCol.withAlpha (0.9f));
            g.strokePath (makeCurve (smoothOther, r, false), juce::PathStrokeType (1.4f));
        }

        g.setGradientFill (juce::ColourGradient (shownCol.withAlpha (0.55f), 0.0f, r.getY(),
                                                 shownCol.withAlpha (0.05f), 0.0f, r.getBottom(), false));
        g.fillPath (makeCurve (smoothShown, r, true));
        g.setColour (shownCol);
        g.strokePath (makeCurve (smoothShown, r, false), juce::PathStrokeType (1.6f));

        if (targetSlope > 0.0f)
        {
            juce::Path t;
            for (int i = 0; i < analyzer.numBands(); ++i)
            {
                const double f = analyzer.bandCentre (i);
                const float x = xForFreq (f, r), y = yForDb (targetShape (f) + targetOffset, r);
                if (i == 0) t.startNewSubPath (x, y); else t.lineTo (x, y);
            }
            juce::Path dashed;
            const float dash[] = { 6.0f, 4.0f };
            juce::PathStrokeType (1.4f).createDashedStroke (dashed, t, dash, 2);
            g.setColour (p.text.withAlpha (0.75f));
            g.fillPath (dashed);
        }
        if (showEq) paintEqCurve (g, r);
        g.restoreState();
    }

    void paintEqCurve (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        const auto& p = pal();
        const double fs = juce::jmax (8000.0, proc.getCurrentSampleRate());
        const auto chains = bm::dsp::EqChains::build (eqParams, fs);
        auto yEq = [&] (double db) { return r.getCentreY() - (float) (juce::jlimit (-14.0, 14.0, db) / 12.0) * r.getHeight() * 0.36f; };

        g.setColour (p.accent.withAlpha (0.35f));
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

        juce::Path mid, side;
        bool differs = false;
        const int n = 160;
        for (int i = 0; i < n; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, (double) i / (n - 1));
            const double dm = bm::dsp::chainResponseDb (chains.mid, chains.nMid, f, fs);
            const double ds = bm::dsp::chainResponseDb (chains.side, chains.nSide, f, fs);
            if (std::fabs (dm - ds) > 0.05) differs = true;
            const float x = xForFreq (f, r);
            if (i == 0) { mid.startNewSubPath (x, yEq (dm)); side.startNewSubPath (x, yEq (ds)); }
            else        { mid.lineTo (x, yEq (dm)); side.lineTo (x, yEq (ds)); }
        }

        g.setColour (p.accent);
        g.strokePath (mid, juce::PathStrokeType (2.2f));
        if (differs)
        {
            juce::Path dashed;
            const float dash[] = { 5.0f, 4.0f };
            juce::PathStrokeType (1.8f).createDashedStroke (dashed, side, dash, 2);
            g.setColour (p.good);
            g.fillPath (dashed);
        }

        g.setColour (p.accent);
        g.setFont (font (10.5f, true));
        g.drawText (juce::String (differs ? "EQ  mid solid / side dashed   " : "EQ   ") + "scale +/-12 dB", r.reduced (6.0f, 4.0f).withHeight (14.0f), juce::Justification::centredLeft);
    }

    static juce::Colour spectrogramColour (float db)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db - kBottom) / (-20.0f - kBottom));
        const juce::Colour stops[] = { juce::Colour (0xff0b1020), juce::Colour (0xff2a1a6e), juce::Colour (0xff9b2a8f),
                                       juce::Colour (0xfff2652a), juce::Colour (0xfffff2c2) };
        const float pos = t * 4.0f;
        const int i = juce::jmin (3, (int) pos);
        return stops[i].interpolatedWith (stops[i + 1], pos - (float) i);
    }

    float dbAtFreq (double f) const
    {
        const int n = analyzer.numBands();
        const double idx = (double) bpo * std::log2 (f / analyzer.bandCentre (0));
        if (idx <= 0.0) return smoothShown[0];
        if (idx >= n - 1) return smoothShown[(size_t) (n - 1)];
        const int i0 = (int) idx;
        const float fr = (float) (idx - i0);
        return smoothShown[(size_t) i0] * (1.0f - fr) + smoothShown[(size_t) (i0 + 1)] * fr;
    }

    void pushSpectrogramColumn()
    {
        if (! specImage.isValid() || analyzer.numBands() < 2 || smoothShown.size() != (size_t) analyzer.numBands()) return;
        const int h = specImage.getHeight();
        for (int y = 0; y < h; ++y)
        {
            const double f = 20.0 * std::pow (1000.0, 1.0 - (double) y / (double) (h - 1)); // low frequencies at the bottom
            specImage.setPixelAt (specCol, y, spectrogramColour (dbAtFreq (f)));
        }
        specCol = (specCol + 1) % specImage.getWidth();
    }

    void paintSpectrogram (juce::Graphics& g)
    {
        const auto& p = pal();
        if (! specImage.isValid()) return;
        const auto r = juce::Rectangle<int> (specArea.getX() + 34, specArea.getY(), specImage.getWidth(), specImage.getHeight());

        g.setColour (juce::Colour (0xff0b1020));
        g.fillRect (r);

        const int w = specImage.getWidth(), h = specImage.getHeight();
        const int oldest = specCol; // the column about to be overwritten is the oldest
        g.drawImage (specImage, r.getX(), r.getY(), w - oldest, h, oldest, 0, w - oldest, h);
        if (oldest > 0)
            g.drawImage (specImage, r.getX() + (w - oldest), r.getY(), oldest, h, 0, 0, oldest, h);

        g.setColour (p.textDim);
        g.setFont (font (10.0f));
        const double marks[] = { 100, 1000, 10000 };
        for (double f : marks)
        {
            const float y = (float) r.getY() + (float) h * (1.0f - (float) (std::log (f / 20.0) / std::log (1000.0)));
            g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f),
                        juce::Rectangle<float> ((float) specArea.getX(), y - 6.0f, 30.0f, 12.0f), juce::Justification::centredRight);
        }
        g.drawText ("SPECTROGRAM  (about 10 s)", juce::Rectangle<int> (r.getX(), r.getBottom() + 1, 240, 12), juce::Justification::centredLeft);
    }

    BroadcastMasterProcessor& proc;
    bm::SpectrumAnalyzer analyzer;
    bm::Weighting weighting = bm::Weighting::Flat;
    float targetSlope = 0.0f, targetOffset = -60.0f;
    int bpo = 6, lastBpo = -1;
    double lastFs = 0.0;
    bool showOther = false, showingOutput = true, showEq = false;
    bm::dsp::EqParams eqParams;
    std::vector<float> raw, smoothShown, smoothOther;
    juce::Rectangle<int> rtaArea, specArea;
    juce::Image specImage;
    int specCol = 0;
};
} // namespace bm::ui
