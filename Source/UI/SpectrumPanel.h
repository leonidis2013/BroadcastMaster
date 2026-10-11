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

    // Draws the tonal EQ curve (mid and side) and one draggable numbered marker per band over the analyser while the EQ stage is selected.
    void setEqOverlay (bool show, const bm::dsp::EqParams& params)
    {
        showEq = show;
        eqParams = params;
    }

    // Shows the multiband compressor graph (band regions, thresholds, levels, reduction) in the analyser window.
    void setMbcOverlay (bool show)
    {
        if (show != showMbc) { showMbc = show; dragKind = 0; setMouseCursor (juce::MouseCursor::NormalCursor); repaint(); }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (showEq)
        {
            const int h = hitEqNode (e.position);
            setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            if (h != eqHover) { eqHover = h; repaint(); }
            return;
        }
        if (! showMbc) return;
        const auto h = mbcHit (e.position);
        setMouseCursor (h.kind == 1 ? juce::MouseCursor::LeftRightResizeCursor
                      : h.kind == 2 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
        if (h.kind != hover.kind || h.idx != hover.idx) { hover = h; repaint(); }
    }

    void mouseExit (const juce::MouseEvent&) override { hover = {}; eqHover = -1; }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (showEq)
        {
            eqDragBand = hitEqNode (e.position);
            if (eqDragBand >= 0)
            {
                proc.apvts.state.setProperty ("eqBand", eqDragBand, nullptr);
                eqFreqParam = proc.apvts.getParameter (bm::params::eqId (eqDragBand, "freq"));
                eqGainParam = proc.apvts.getParameter (bm::params::eqId (eqDragBand, "gain"));
                if (eqFreqParam) eqFreqParam->beginChangeGesture();
                if (eqGainParam) eqGainParam->beginChangeGesture();
            }
            return;
        }
        if (! showMbc || ! plotRect().contains (e.position)) return;
        const auto h = mbcHit (e.position);
        if (h.band >= 0) proc.apvts.state.setProperty ("mbcBand", h.band, nullptr);
        dragKind = h.kind;
        dragIdx = h.idx;
        dragParam = nullptr;
        if (h.kind == 1) dragParam = proc.apvts.getParameter ("mb_x" + juce::String (h.idx + 1));
        else if (h.kind == 2) dragParam = proc.apvts.getParameter (bm::params::mbId (h.idx, "thr"));
        if (dragParam != nullptr) dragParam->beginChangeGesture();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (showEq)
        {
            if (eqDragBand < 0) return;
            const auto r = plotRect();
            if (eqFreqParam) eqFreqParam->setValueNotifyingHost (eqFreqParam->convertTo0to1 (freqForX (e.position.x, r)));
            if (eqGainParam) eqGainParam->setValueNotifyingHost (eqGainParam->convertTo0to1 (juce::jlimit (-12.0f, 12.0f, eqDbForY (e.position.y, r))));
            return;
        }
        if (! showMbc || dragParam == nullptr) return;
        const auto r = plotRect();
        if (dragKind == 1)
        {
            const auto* xs = bm::params::findSpec ("mb_x" + std::to_string (dragIdx + 1));
            double f = 20.0 * std::pow (1000.0, juce::jlimit (0.0, 1.0, (double) ((e.position.x - r.getX()) / r.getWidth())));
            double lo = xs->min, hi = xs->max;
            if (dragIdx > 0) lo = juce::jmax (lo, (double) param ("mb_x" + juce::String (dragIdx)) * 1.3);
            if (dragIdx < 2) hi = juce::jmin (hi, (double) param ("mb_x" + juce::String (dragIdx + 2)) / 1.3);
            if (lo < hi) dragParam->setValueNotifyingHost (dragParam->convertTo0to1 ((float) juce::jlimit (lo, hi, f)));
        }
        else if (dragKind == 2)
        {
            const float db = -kMbcRange * juce::jlimit (0.0f, 1.0f, (e.position.y - r.getY()) / r.getHeight());
            dragParam->setValueNotifyingHost (dragParam->convertTo0to1 (db));
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (eqFreqParam) eqFreqParam->endChangeGesture();
        if (eqGainParam) eqGainParam->endChangeGesture();
        eqFreqParam = eqGainParam = nullptr; eqDragBand = -1;
        if (dragParam != nullptr) dragParam->endChangeGesture();
        dragParam = nullptr; dragKind = 0;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (showEq)
        {
            const int b = hitEqNode (e.position);   // double-click a marker: back to 0 dB
            if (b >= 0)
                if (auto* prm = proc.apvts.getParameter (bm::params::eqId (b, "gain")))
                { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (0.0f)); prm->endChangeGesture(); }
            return;
        }
        if (! showMbc) return;
        const auto h = mbcHit (e.position);
        const juce::String id = h.kind == 1 ? juce::String ("mb_x") + juce::String (h.idx + 1)
                              : h.kind == 2 ? juce::String (bm::params::mbId (h.idx, "thr")) : juce::String();
        if (id.isEmpty()) return;
        if (auto* prm = proc.apvts.getParameter (id))
            if (const auto* sp = bm::params::findSpec (id.toStdString()))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 (sp->def));
                prm->endChangeGesture();
            }
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

        if (showMbc) { paintMbc (g, full, r); return; }

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
        if (showEq) paintEqNodes (g, r);
    }

    // ---- multiband compressor graph -------------------------------------------------------------------
    static constexpr float kMbcRange = 60.0f;   // vertical scale: 0 dB at the top, -60 dB at the bottom

    juce::Rectangle<float> plotRect() const { return rtaArea.toFloat().withTrimmedLeft (34.0f).withTrimmedBottom (16.0f); }
    float param (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }
    float yForMbc (float db, juce::Rectangle<float> r) const { return r.getY() + r.getHeight() * (-juce::jlimit (-kMbcRange, 0.0f, db) / kMbcRange); }
    int selectedBand() const { return juce::jlimit (0, 3, (int) proc.apvts.state.getProperty ("mbcBand", 0)); }

    struct MbcHit { int kind = 0; int idx = -1; int band = -1; };   // kind: 0 none, 1 crossover, 2 threshold

    MbcHit mbcHit (juce::Point<float> pt) const
    {
        MbcHit h;
        const auto r = plotRect();
        if (! r.contains (pt)) return h;
        const double xo[3] { param ("mb_x1"), param ("mb_x2"), param ("mb_x3") };
        const double edges[5] { 20.0, xo[0], xo[1], xo[2], 20000.0 };
        for (int b = 0; b < 4; ++b)
            if (pt.x >= xForFreq (edges[b], r) && pt.x <= xForFreq (edges[b + 1], r)) h.band = b;
        for (int i = 0; i < 3; ++i)
            if (std::fabs (pt.x - xForFreq (xo[i], r)) < 7.0f) { h.kind = 1; h.idx = i; return h; }
        if (h.band >= 0 && std::fabs (pt.y - yForMbc (param (bm::params::mbId (h.band, "thr")), r)) < 9.0f)
        { h.kind = 2; h.idx = h.band; }
        return h;
    }

    // Tonal EQ scale: 0 dB at the middle of the analyser, +/-12 dB to either side.
    static float yForEq (float db, juce::Rectangle<float> r) { return r.getCentreY() - juce::jlimit (-14.0f, 14.0f, db) / 12.0f * r.getHeight() * 0.36f; }
    static float eqDbForY (float y, juce::Rectangle<float> r) { return (r.getCentreY() - y) / (r.getHeight() * 0.36f) * 12.0f; }
    float freqForX (float x, juce::Rectangle<float> r) const { return (float) (20.0 * std::pow (1000.0, juce::jlimit (0.0, 1.0, (double) ((x - r.getX()) / r.getWidth())))); }
    int selectedEqBand() const { return juce::jlimit (0, bm::dsp::EqParams::kBands - 1, (int) proc.apvts.state.getProperty ("eqBand", 0)); }

    juce::Point<float> eqNodePos (int b, juce::Rectangle<float> r) const
    {
        const auto& bd = eqParams.band[b];
        return { xForFreq (bd.freq, r), yForEq (bd.gainDb, r) };
    }

    int hitEqNode (juce::Point<float> pt) const
    {
        const auto r = plotRect();
        int best = -1; float bestD = 12.0f;
        for (int b = 0; b < bm::dsp::EqParams::kBands; ++b)
        {
            const float d = eqNodePos (b, r).getDistanceFrom (pt);
            if (d < bestD) { bestD = d; best = b; }
        }
        return best;
    }

    // One numbered marker per band, sitting at the band's frequency and gain. Drag it to change both.
    void paintEqNodes (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        const auto& p = pal();
        const int sel = selectedEqBand();
        for (int b = 0; b < bm::dsp::EqParams::kBands; ++b)
        {
            const bool on = eqParams.band[b].on;
            const auto pos = eqNodePos (b, r);
            const auto col = p.eqCol[b];
            const bool hot = b == eqHover || b == eqDragBand;
            const float rad = (b == sel ? 9.5f : 8.0f) + (hot ? 1.0f : 0.0f);
            // a faint line down to the frequency axis shows exactly where the band sits
            g.setColour (col.withAlpha (on ? 0.35f : 0.12f));
            g.drawVerticalLine ((int) pos.x, r.getY(), r.getBottom());
            g.setColour (on ? col.withAlpha (0.28f) : p.panel);
            g.fillEllipse (pos.x - rad, pos.y - rad, rad * 2.0f, rad * 2.0f);
            g.setColour (on ? col : p.textDim);
            g.drawEllipse (pos.x - rad, pos.y - rad, rad * 2.0f, rad * 2.0f, b == sel ? 2.0f : 1.3f);
            g.setFont (font (10.5f, true));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (pos.x - rad, pos.y - rad, rad * 2.0f, rad * 2.0f), juce::Justification::centred);
            if (b == sel || hot)
            {
                g.setColour (p.text);
                g.setFont (monoFont (10.0f, true));
                const float tx = juce::jlimit (r.getX() + 2.0f, r.getRight() - 112.0f, pos.x + rad + 4.0f);
                g.drawText (hzText (eqParams.band[b].freq) + "  " + signedText (eqParams.band[b].gainDb, 1) + " dB",
                            juce::Rectangle<float> (tx, pos.y - 7.0f, 110.0f, 14.0f), juce::Justification::centredLeft);
            }
        }
    }

    static juce::String hzText (double f) { return f >= 1000.0 ? juce::String (f / 1000.0, f >= 10000.0 ? 1 : 2) + " kHz" : juce::String ((int) std::lround (f)) + " Hz"; }

    void paintMbc (juce::Graphics& g, juce::Rectangle<float> full, juce::Rectangle<float> r)
    {
        const auto& p = pal();
        g.setFont (font (10.0f));
        for (float db = 0.0f; db >= -kMbcRange - 0.1f; db -= 10.0f)
        {
            const float y = yForMbc (db, r);
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

        const bool stageOn = param ("en_mbc") > 0.5f;
        const int sel = selectedBand();
        const double xo[3] { param ("mb_x1"), param ("mb_x2"), param ("mb_x3") };
        const double edges[5] { 20.0, xo[0], xo[1], xo[2], 20000.0 };
        static const char* names[] { "LOW", "LOW-MID", "HIGH-MID", "HIGH" };

        g.saveState();
        g.reduceClipRegion (r.toNearestInt());

        for (int b = 0; b < 4; ++b)
        {
            const float x0 = xForFreq (edges[b], r), x1 = xForFreq (edges[b + 1], r);
            const auto region = juce::Rectangle<float> (x0, r.getY(), x1 - x0, r.getHeight());
            const auto col = p.bandCol[b];
            const bool live = stageOn && param (bm::params::mbId (b, "on")) > 0.5f;
            const float a = live ? 1.0f : 0.4f;
            const float thr = param (bm::params::mbId (b, "thr"));
            const float yT = yForMbc (thr, r);

            g.setColour (col.withAlpha ((b == sel ? 0.13f : 0.06f) * a));
            g.fillRect (region);
            g.setColour (col.withAlpha (0.05f * a));
            g.fillRect (region.withBottom (yT));          // the zone where this band is compressed

            // detector level
            const float lvl = live ? proc.mbcBandLevel (b) : -120.0f;
            const float bw = juce::jmin (26.0f, region.getWidth() * 0.18f);
            const float bx = region.getX() + region.getWidth() * 0.27f - bw * 0.5f;
            if (lvl > -kMbcRange)
            {
                const float yL = yForMbc (lvl, r);
                g.setGradientFill (juce::ColourGradient (col.withAlpha (0.65f), 0.0f, yL, col.withAlpha (0.12f), 0.0f, r.getBottom(), false));
                g.fillRect (juce::Rectangle<float> (bx, yL, bw, r.getBottom() - yL));
                g.setColour (col);
                g.fillRect (juce::Rectangle<float> (bx, yL - 1.0f, bw, 2.0f));
            }

            // gain reduction hangs from the top
            const float gr = live ? proc.mbcBandGr (b) : 0.0f;
            const float gx = region.getX() + region.getWidth() * 0.73f - 6.0f;
            g.setColour (p.border.withAlpha (0.7f));
            g.fillRoundedRectangle (gx, r.getY() + 34.0f, 12.0f, r.getHeight() * 0.55f, 3.0f);
            const float grH = r.getHeight() * 0.55f * juce::jlimit (0.0f, 1.0f, gr / 18.0f);
            g.setColour (p.reduction);
            g.fillRoundedRectangle (gx, r.getY() + 34.0f, 12.0f, juce::jmax (0.0f, grH), 3.0f);
            g.setColour (p.textDim);
            g.setFont (monoFont (10.0f));
            g.drawText ((gr < 0.05f ? juce::String ("0.0") : juce::String (-gr, 1)) + " dB", juce::Rectangle<float> (gx - 24.0f, r.getY() + 36.0f + r.getHeight() * 0.55f, 60.0f, 12.0f), juce::Justification::centred);

            // threshold line and handle
            const bool hot = (hover.kind == 2 && hover.idx == b) || (dragKind == 2 && dragIdx == b);
            g.setColour (col.withAlpha (a));
            g.fillRect (juce::Rectangle<float> (region.getX() + 6.0f, yT - 1.0f, region.getWidth() - 12.0f, hot ? 3.0f : 2.0f));
            const auto hc = juce::Point<float> (region.getCentreX(), yT);
            g.fillEllipse (juce::Rectangle<float> (hot ? 16.0f : 13.0f, hot ? 16.0f : 13.0f).withCentre (hc));
            g.setColour (p.bg);
            g.drawEllipse (juce::Rectangle<float> (hot ? 16.0f : 13.0f, hot ? 16.0f : 13.0f).withCentre (hc), 1.5f);
            g.setColour (p.text.withAlpha (a));
            g.setFont (monoFont (10.5f, true));
            const float ly = yT < r.getY() + 54.0f ? yT + 8.0f : yT - 20.0f;
            g.drawText (juce::String (thr, 1) + " dB   " + juce::String (param (bm::params::mbId (b, "ratio")), 1) + ":1",
                        juce::Rectangle<float> (region.getX() + 8.0f, ly, region.getWidth() - 16.0f, 14.0f), juce::Justification::centredLeft);

            // title
            g.setColour (col.withAlpha (a));
            g.setFont (font (11.0f, true));
            g.drawText (juce::String (b + 1) + "  " + names[b], juce::Rectangle<float> (region.getX() + 8.0f, r.getY() + 5.0f, region.getWidth() - 16.0f, 14.0f), juce::Justification::centredLeft);
            g.setColour (p.textDim);
            g.setFont (font (10.0f));
            const juce::String range = b == 0 ? "below " + hzText (xo[0]) : (b == 3 ? "above " + hzText (xo[2]) : hzText (xo[b - 1]) + " - " + hzText (xo[b]));
            g.drawText (range, juce::Rectangle<float> (region.getX() + 8.0f, r.getY() + 19.0f, region.getWidth() - 16.0f, 12.0f), juce::Justification::centredLeft);

            if (b == sel)
            {
                g.setColour (col);
                g.fillRect (juce::Rectangle<float> (region.getX(), r.getBottom() - 3.0f, region.getWidth(), 3.0f));
            }
        }

        // crossover lines and handles
        for (int i = 0; i < 3; ++i)
        {
            const float x = xForFreq (xo[i], r);
            const bool hot = (hover.kind == 1 && hover.idx == i) || (dragKind == 1 && dragIdx == i);
            g.setColour (p.text.withAlpha (hot ? 0.9f : 0.45f));
            g.fillRect (juce::Rectangle<float> (x - (hot ? 1.0f : 0.5f), r.getY(), hot ? 2.0f : 1.0f, r.getHeight()));
            g.setColour (p.panelRaised);
            const auto pill = juce::Rectangle<float> (10.0f, 30.0f).withCentre ({ x, r.getCentreY() });
            g.fillRoundedRectangle (pill, 4.0f);
            g.setColour (p.text.withAlpha (hot ? 0.9f : 0.55f));
            g.drawRoundedRectangle (pill, 4.0f, 1.0f);
            g.setFont (monoFont (10.0f, true));
            g.drawText (hzText (xo[i]), juce::Rectangle<float> (x - 36.0f, r.getBottom() - 34.0f, 72.0f, 12.0f), juce::Justification::centred);
        }
        g.restoreState();

        g.setColour (p.textDim);
        g.setFont (font (10.5f));
        g.drawText ("MULTIBAND   bars: band level   pink: reduction   line: threshold",
                    getLocalBounds().toFloat().reduced (12.0f).withHeight (18.0f).withTrimmedRight (80.0f), juce::Justification::centredRight);
    }

    void paintEqCurve (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        const auto& p = pal();
        const double fs = juce::jmax (8000.0, proc.getCurrentSampleRate());
        const auto chains = bm::dsp::EqChains::build (eqParams, fs);
        auto yEq = [&] (double db) { return yForEq ((float) db, r); };

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
        const double marks[] = { 20, 100, 1000, 10000, 20000 };
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
    bool showOther = false, showingOutput = true, showEq = false, showMbc = false;
    MbcHit hover;
    int eqHover = -1, eqDragBand = -1;
    juce::RangedAudioParameter *eqFreqParam = nullptr, *eqGainParam = nullptr;
    int dragKind = 0, dragIdx = -1;
    juce::RangedAudioParameter* dragParam = nullptr;
    bm::dsp::EqParams eqParams;
    std::vector<float> raw, smoothShown, smoothOther;
    juce::Rectangle<int> rtaArea, specArea;
    juce::Image specImage;
    int specCol = 0;
};
} // namespace bm::ui
