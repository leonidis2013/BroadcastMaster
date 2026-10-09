#pragma once
#include <functional>
#include <memory>
#include <vector>
#include "ParamControls.h"

namespace bm::ui
{
inline const char* stageDescription (int stage)
{
    switch (stage)
    {
        case bm::dsp::sInput:  return "Gain before anything else. Use it to bring the incoming level to a sensible working level for the rest of the chain.";
        case bm::dsp::sHpf:    return "Removes inaudible sub-bass rumble (4th-order, 24 dB/oct) so the leveler and limiter do not react to energy nobody can hear.";
        case bm::dsp::sPhase:  return "All-pass filters that change the waveform shape but not the tone. They lower the peaks of asymmetric voice waveforms, giving the limiter more headroom.";
        case bm::dsp::sEq:     return "";
        case bm::dsp::sSat:    return "Adds harmonic warmth. Tube adds even harmonics, Tape adds soft odd harmonics with a gentle top-end roll-off, Transformer saturates the low end harder. Anti-aliased.";
        case bm::dsp::sExc:    return "The exciter adds air above the chosen frequency. The bass enhancer adds harmonics of the low end so bass stays audible on small speakers. Both add to the untouched signal.";
        case bm::dsp::sMbc:    return "";
        case bm::dsp::sLev:    return "A slow gain rider that moves the programme toward the target loudness. It holds still when the signal drops below the gate, so it never pumps up room noise. Auto mode uses a speech/music detector, which is a heuristic: choose Speech or Music if it guesses wrong.";
        case bm::dsp::sStereo: return "Width, mid and side gain, polarity flips and mono sum. The lip-sync delay (0 to 500 ms) is intentional and is NOT reported to the host as latency.";
        case bm::dsp::sClip:   return "A smooth tanh shoulder that rounds peaks before the limiter, so the limiter has less to do. The linear zone is how much of the range stays untouched.";
        case bm::dsp::sLim:    return "Look-ahead limiter working on the 4x oversampled (true) peak. The ceiling is the knob on this stage's tile. Look-ahead is 1 ms in Live mode and 5 ms in Broadcast mode. Content right up against Nyquist can still overshoot by a fraction of a dB.";
        case bm::dsp::sOut:    return "A cut-only trim after the limiter (0 to -24 dB), for the last bit of level matching to the destination.";
        default: return "";
    }
}

// The panel for one stage, built from the parameter descriptions: knobs, drop-downs and switches.
class GenericModulePanel : public juce::Component
{
public:
    GenericModulePanel (int stageIndex, BroadcastMasterProcessor& p, std::function<juce::String()> live = {})
        : stage (stageIndex), proc (p), liveText (std::move (live))
    {
        for (const auto& s : bm::params::moduleSpecs())
            if (s.stage == stage && ! s.custom)
            {
                items.push_back ({ std::make_unique<ParamControl> (p.apvts, s, ParamControl::Style::Knob),
                                   ParamControl::preferredWidth (s, ParamControl::Style::Knob),
                                   ParamControl::preferredHeight (s, ParamControl::Style::Knob), s.group });
                addAndMakeVisible (*items.back().control);
            }

        if (const char* en = bm::params::kStages[stage].enableId)
        {
            enable = std::make_unique<StageEnableToggle> (p.apvts, en);
            addAndMakeVisible (*enable);
        }
        description = stageDescription (stage);
    }

    void tick() { if (liveText) { auto t = liveText(); if (t != lastLive) { lastLive = t; repaint(); } } }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, getLocalBounds().toFloat(), bm::params::kStages[stage].name);

        g.setColour (p.textDim);
        g.setFont (font (11.5f));
        g.drawFittedText (description, descArea, juce::Justification::topLeft, 8, 1.0f);

        if (lastLive.isNotEmpty())
        {
            g.setColour (p.text);
            g.setFont (monoFont (12.0f));
            g.drawFittedText (lastLive, liveArea, juce::Justification::bottomLeft, 4, 1.0f);
        }

        g.setColour (p.textDim);
        g.setFont (font (10.5f, true));
        for (const auto& h : headings)
            g.drawText (h.text.toUpperCase(), h.area, juce::Justification::centredLeft);

        if (items.empty())
        {
            g.setColour (p.textDim);
            g.setFont (font (12.0f));
            g.drawText (stage == bm::dsp::sInput || stage == bm::dsp::sOut ? "The level control for this stage is the knob on its tile above."
                                                                           : "No further settings. The gain knob on this stage's tile sets its output level.",
                        controlsArea.reduced (8), juce::Justification::topLeft);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 6);
        auto title = r.removeFromTop (20);
        if (enable) enable->setBounds (title.removeFromRight (110));
        r.removeFromTop (4);

        const int descW = juce::jlimit (240, 340, r.getWidth() / 3);
        auto right = r.removeFromRight (descW);
        r.removeFromRight (12);
        descArea = right.removeFromTop (juce::jmax (60, right.getHeight() - 56));
        liveArea = right;
        controlsArea = r;

        headings.clear();
        int x = r.getX(), y = r.getY(), rowH = 0;
        juce::String lastGroup;
        for (auto& it : items)
        {
            const bool newGroup = it.group != lastGroup;
            if (newGroup && it.group.isNotEmpty())
            {
                if (x > r.getX()) { x = r.getX(); y += rowH + 8; rowH = 0; }
                headings.push_back ({ it.group, juce::Rectangle<int> (x, y, 220, 16) });
                y += 18;
            }
            lastGroup = it.group;

            if (x + it.w > r.getRight() && x > r.getX()) { x = r.getX(); y += rowH + 8; rowH = 0; }
            it.control->setBounds (x, y, it.w, it.h);
            x += it.w + 10;
            rowH = juce::jmax (rowH, it.h);
        }
    }

private:
    struct Item { std::unique_ptr<ParamControl> control; int w, h; juce::String group; };
    struct Heading { juce::String text; juce::Rectangle<int> area; };

    int stage;
    BroadcastMasterProcessor& proc;
    std::function<juce::String()> liveText;
    juce::String lastLive, description;
    std::vector<Item> items;
    std::vector<Heading> headings;
    std::unique_ptr<StageEnableToggle> enable;
    juce::Rectangle<int> descArea, liveArea, controlsArea;
};

// Six-band mid/side EQ plus tilt.
class EqPanel : public juce::Component
{
public:
    explicit EqPanel (BroadcastMasterProcessor& p) : proc (p)
    {
        using bm::params::eqId;
        auto spec = [] (const std::string& id) { return bm::params::findSpec (id); };
        for (int b = 0; b < bm::dsp::EqParams::kBands; ++b)
        {
            const bool shelf = b == 0 || b == bm::dsp::EqParams::kBands - 1;
            add (on[(size_t) b], p, *spec (eqId (b, "on")), ParamControl::Style::Bar, "On");
            add (freq[(size_t) b], p, *spec (eqId (b, "freq")), ParamControl::Style::Bar, "Freq", 40);
            add (gain[(size_t) b], p, *spec (eqId (b, "gain")), ParamControl::Style::Bar, "Gain", 40);
            add (q[(size_t) b], p, *spec (eqId (b, "q")), ParamControl::Style::Bar, shelf ? "Slope" : "Q", 40);
            add (target[(size_t) b], p, *spec (eqId (b, "tgt")), ParamControl::Style::Bar, "Acts on", 52);
        }
        add (tilt, p, *spec ("eq_tilt"), ParamControl::Style::Bar, "Tilt", 34);
        enable = std::make_unique<StageEnableToggle> (p.apvts, "en_eq");
        addAndMakeVisible (*enable);
    }

    void tick() {}

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, getLocalBounds().toFloat(), "Tonal EQ");
        static const char* names[] { "LOW SHELF", "BELL 1", "BELL 2", "BELL 3", "BELL 4", "HIGH SHELF" };
        g.setColour (p.textDim);
        g.setFont (font (10.5f, true));
        for (int b = 0; b < bm::dsp::EqParams::kBands; ++b)
            g.drawText (names[b], headerRects[(size_t) b], juce::Justification::centredLeft);
        g.drawText ("TILT", tiltHeader, juce::Justification::centredLeft);

        g.setFont (font (11.0f));
        g.drawFittedText ("Each band can act on the whole signal, the mid only or the side only. The curve is drawn over the spectrum. "
                          "Live mode = minimum phase, no latency. Broadcast mode = linear phase, adds latency. Tilt is a broad see-saw around 1 kHz.",
                          noteArea, juce::Justification::topLeft, 6, 1.0f);

        g.setColour (p.border);
        for (int b = 1; b <= bm::dsp::EqParams::kBands; ++b)
            g.drawVerticalLine (colX[(size_t) b] - 5, (float) headerRects[0].getY(), (float) getHeight() - 8.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 6);
        auto title = r.removeFromTop (20);
        enable->setBounds (title.removeFromRight (110));
        r.removeFromTop (6);

        const int cols = bm::dsp::EqParams::kBands + 1;
        const int w = r.getWidth() / cols;
        for (int c = 0; c <= cols; ++c) colX[(size_t) c] = r.getX() + w * c;

        for (int b = 0; b < bm::dsp::EqParams::kBands; ++b)
        {
            auto col = juce::Rectangle<int> (colX[(size_t) b], r.getY(), w - 10, r.getHeight());
            auto head = col.removeFromTop (22);
            on[(size_t) b]->setBounds (head.removeFromRight (50));
            headerRects[(size_t) b] = head;
            col.removeFromTop (4);
            for (auto* c : { freq[(size_t) b].get(), gain[(size_t) b].get(), q[(size_t) b].get(), target[(size_t) b].get() })
            {
                c->setBounds (col.removeFromTop (30));
            }
        }

        auto col = juce::Rectangle<int> (colX[(size_t) (cols - 1)], r.getY(), w - 6, r.getHeight());
        tiltHeader = col.removeFromTop (22);
        col.removeFromTop (4);
        tilt->setBounds (col.removeFromTop (30));
        col.removeFromTop (6);
        noteArea = col;
    }

private:
    void add (std::unique_ptr<ParamControl>& slot, BroadcastMasterProcessor& p, const bm::params::ParamSpec& s,
              ParamControl::Style st, const juce::String& label, int labelW = 56)
    {
        slot = std::make_unique<ParamControl> (p.apvts, s, st, label, labelW);
        addAndMakeVisible (*slot);
    }

    BroadcastMasterProcessor& proc;
    std::array<std::unique_ptr<ParamControl>, bm::dsp::EqParams::kBands> on, freq, gain, q, target;
    std::unique_ptr<ParamControl> tilt;
    std::unique_ptr<StageEnableToggle> enable;
    std::array<juce::Rectangle<int>, bm::dsp::EqParams::kBands> headerRects;
    std::array<int, bm::dsp::EqParams::kBands + 2> colX {};
    juce::Rectangle<int> tiltHeader, noteArea;
};

// Four-band multiband compressor: per-band controls with a gain-reduction bar, plus the crossovers.
class MbcPanel : public juce::Component
{
public:
    explicit MbcPanel (BroadcastMasterProcessor& p) : proc (p)
    {
        using bm::params::mbId;
        auto spec = [] (const std::string& id) { return bm::params::findSpec (id); };
        for (int b = 0; b < bm::dsp::MbcParams::kBands; ++b)
        {
            add (on[(size_t) b], *spec (mbId (b, "on")), "On");
            add (thr[(size_t) b], *spec (mbId (b, "thr")), "Thresh", 52);
            add (ratio[(size_t) b], *spec (mbId (b, "ratio")), "Ratio", 52);
            add (atk[(size_t) b], *spec (mbId (b, "atk")), "Attack", 52);
            add (rel[(size_t) b], *spec (mbId (b, "rel")), "Release", 52);
            add (mk[(size_t) b], *spec (mbId (b, "mk")), "Makeup", 52);

            auto& s = solo[(size_t) b];
            s.setButtonText ("S");
            s.setClickingTogglesState (false);
            s.setTooltip ("Listen to this band on the solo bus. Uses Monitor B and switches the solo bus on.");
            s.onClick = [this, b] { toggleSolo (b); };
            addAndMakeVisible (s);
        }
        add (x1, *spec ("mb_x1"), "Xover 1", 56);
        add (x2, *spec ("mb_x2"), "Xover 2", 56);
        add (x3, *spec ("mb_x3"), "Xover 3", 56);
        add (knee, *spec ("mb_knee"), "Knee", 56);
        add (mix, *spec ("mb_mix"), "Mix", 56);
        enable = std::make_unique<StageEnableToggle> (p.apvts, "en_mbc");
        addAndMakeVisible (*enable);
    }

    void tick()
    {
        for (int b = 0; b < bm::dsp::MbcParams::kBands; ++b)
        {
            const float gr = proc.mbcBandGr (b);
            shownGr[(size_t) b] += (gr - shownGr[(size_t) b]) * 0.4f;
            solo[(size_t) b].setToggleState (isSoloing (b), juce::dontSendNotification);
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, getLocalBounds().toFloat(), "Multiband Compressor");

        const float f1 = raw ("mb_x1"), f2 = raw ("mb_x2"), f3 = raw ("mb_x3");
        const juce::String ranges[4] { "below " + hz (f1), hz (f1) + " - " + hz (f2), hz (f2) + " - " + hz (f3), "above " + hz (f3) };

        for (int b = 0; b < bm::dsp::MbcParams::kBands; ++b)
        {
            g.setColour (p.textDim);
            g.setFont (font (10.5f, true));
            const auto hr = headerRects[(size_t) b];
            g.drawText ("BAND " + juce::String (b + 1), hr.withHeight (14), juce::Justification::centredLeft);
            g.setFont (font (10.0f));
            g.drawText (ranges[b], hr.withTrimmedTop (13), juce::Justification::centredLeft);

            // gain reduction bar, 0 to 18 dB, filling downward
            const auto bar = barRects[(size_t) b].toFloat();
            g.setColour (p.bg.withAlpha (p.dark ? 0.7f : 0.5f));
            g.fillRoundedRectangle (bar, 2.0f);
            const float h = bar.getHeight() * juce::jlimit (0.0f, 1.0f, shownGr[(size_t) b] / 18.0f);
            g.setColour (p.warn);
            g.fillRect (bar.withHeight (h));
            g.setColour (p.textDim);
            g.setFont (monoFont (9.0f));
            g.drawText (juce::String (shownGr[(size_t) b], 1), bar.withY (bar.getBottom() + 1.0f).withHeight (11.0f).expanded (8.0f, 0.0f), juce::Justification::centred);
        }

        g.setColour (p.textDim);
        g.setFont (font (10.5f, true));
        g.drawText ("CROSSOVERS AND SHARED", globalHeader, juce::Justification::centredLeft);

        g.setColour (p.border);
        for (int b = 1; b <= bm::dsp::MbcParams::kBands; ++b)
            g.drawVerticalLine (colX[(size_t) b] - 6, (float) headerRects[0].getY(), (float) getHeight() - 8.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 6);
        auto title = r.removeFromTop (20);
        enable->setBounds (title.removeFromRight (110));
        r.removeFromTop (6);

        const int cols = bm::dsp::MbcParams::kBands + 1;
        const int w = r.getWidth() / cols;
        for (int c = 0; c <= cols; ++c) colX[(size_t) c] = r.getX() + w * c;

        for (int b = 0; b < bm::dsp::MbcParams::kBands; ++b)
        {
            auto col = juce::Rectangle<int> (colX[(size_t) b], r.getY(), w - 12, r.getHeight());
            auto head = col.removeFromTop (28);
            solo[(size_t) b].setBounds (head.removeFromRight (24).withSizeKeepingCentre (24, 22));
            head.removeFromRight (4);
            on[(size_t) b]->setBounds (head.removeFromRight (50));
            headerRects[(size_t) b] = head;
            col.removeFromTop (4);

            auto body = col;
            barRects[(size_t) b] = body.removeFromLeft (8).withTrimmedBottom (14);
            body.removeFromLeft (4);
            for (auto* c : { thr[(size_t) b].get(), ratio[(size_t) b].get(), atk[(size_t) b].get(), rel[(size_t) b].get(), mk[(size_t) b].get() })
                c->setBounds (body.removeFromTop (26));
        }

        auto col = juce::Rectangle<int> (colX[(size_t) (cols - 1)], r.getY(), w - 6, r.getHeight());
        globalHeader = col.removeFromTop (28);
        col.removeFromTop (4);
        for (auto* c : { x1.get(), x2.get(), x3.get(), knee.get(), mix.get() })
            c->setBounds (col.removeFromTop (26));
    }

private:
    void add (std::unique_ptr<ParamControl>& slot, const bm::params::ParamSpec& s, const juce::String& label, int labelW = 56)
    {
        slot = std::make_unique<ParamControl> (proc.apvts, s, ParamControl::Style::Bar, label, labelW);
        addAndMakeVisible (*slot);
    }

    float raw (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }
    static juce::String hz (float f) { return f >= 1000.0f ? juce::String (f / 1000.0f, 2) + "k" : juce::String ((int) std::lround (f)); }

    bool isSoloing (int b) const
    {
        return raw (bm::params::soloOn) > 0.5f && raw (bm::params::monAB) > 0.5f
            && (int) std::lround (raw (bm::params::monB)) == (int) bm::dsp::MonSrc::Band1 + b;
    }

    void toggleSolo (int b)
    {
        if (isSoloing (b)) { setParamPlain (proc.apvts, bm::params::soloOn, 0.0f); return; }
        setParamPlain (proc.apvts, bm::params::monB, (float) ((int) bm::dsp::MonSrc::Band1 + b));
        setParamPlain (proc.apvts, bm::params::monAB, 1.0f);
        setParamPlain (proc.apvts, bm::params::soloOn, 1.0f);
    }

    BroadcastMasterProcessor& proc;
    std::array<std::unique_ptr<ParamControl>, 4> on, thr, ratio, atk, rel, mk;
    std::unique_ptr<ParamControl> x1, x2, x3, knee, mix;
    std::array<juce::TextButton, 4> solo;
    std::unique_ptr<StageEnableToggle> enable;
    std::array<juce::Rectangle<int>, 4> headerRects, barRects;
    std::array<int, 6> colX {};
    std::array<float, 4> shownGr {};
    juce::Rectangle<int> globalHeader;
};

// Holds one panel per stage and shows the selected one.
class ModulePanel : public juce::Component
{
public:
    explicit ModulePanel (BroadcastMasterProcessor& p) : proc (p)
    {
        using namespace bm::dsp;
        for (int s = 0; s < bm::params::kNumStages; ++s)
        {
            if (s == sEq)       { eq = std::make_unique<EqPanel> (p); addChildComponent (*eq); continue; }
            if (s == sMbc)      { mbc = std::make_unique<MbcPanel> (p); addChildComponent (*mbc); continue; }

            std::function<juce::String()> live;
            if (s == sLev)
                live = [&p]
                {
                    const float sp = p.levelerSpeech();
                    return "Gain applied: " + signedText (p.levelerGainDb()) + " dB\nContent: " + juce::String (juce::roundToInt (sp * 100.0f)) + "% speech-like";
                };
            if (s == sLim)
                live = [&p]
                {
                    const int lat = p.reportedLatency();
                    return "Reduction now: " + juce::String (p.gainReductionLimiter.load(), 1) + " dB\nChain latency: "
                         + juce::String (lat) + " samples (" + juce::String (lat * 1000.0 / juce::jmax (1.0, p.getCurrentSampleRate()), 1) + " ms)";
                };
            generic[(size_t) s] = std::make_unique<GenericModulePanel> (s, p, live);
            addChildComponent (*generic[(size_t) s]);
        }
        select (selected);
    }

    void select (int stage)
    {
        selected = juce::jlimit (0, bm::params::kNumStages - 1, stage);
        for (int s = 0; s < bm::params::kNumStages; ++s)
        {
            juce::Component* c = s == bm::dsp::sEq ? (juce::Component*) eq.get()
                               : s == bm::dsp::sMbc ? (juce::Component*) mbc.get() : (juce::Component*) generic[(size_t) s].get();
            if (c) c->setVisible (s == selected);
        }
    }

    int getSelected() const noexcept { return selected; }

    void tick()
    {
        if (selected == bm::dsp::sEq) eq->tick();
        else if (selected == bm::dsp::sMbc) mbc->tick();
        else if (generic[(size_t) selected]) generic[(size_t) selected]->tick();
    }

    void resized() override
    {
        const auto r = getLocalBounds();
        if (eq) eq->setBounds (r);
        if (mbc) mbc->setBounds (r);
        for (auto& g : generic) if (g) g->setBounds (r);
    }

private:
    BroadcastMasterProcessor& proc;
    std::unique_ptr<EqPanel> eq;
    std::unique_ptr<MbcPanel> mbc;
    std::array<std::unique_ptr<GenericModulePanel>, bm::params::kNumStages> generic;
    int selected = bm::dsp::sEq;
};
} // namespace bm::ui
