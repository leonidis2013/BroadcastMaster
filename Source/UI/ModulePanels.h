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
        case bm::dsp::sStereo: return "Width, mid and side gain, polarity flips and mono sum. The stereo exciter adds harmonics to the side signal above its frequency, which opens up the top end without changing the mono sum. The lip-sync delay (0 to 500 ms) is intentional and is NOT reported to the host as latency.";
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
                                   ParamControl::preferredHeight (s, ParamControl::Style::Knob), s.group,
                                   s.kind == bm::params::Kind::Choice });
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

    // Everything sits in ONE row: knobs side by side, groups next to each other under their heading, and
    // consecutive switches stacked in a single column. If the row would not fit, the knobs get narrower.
    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 8);
        auto title = r.removeFromTop (20);
        if (enable) enable->setBounds (title.removeFromRight (110));
        r.removeFromTop (6);

        const int gap = 12, groupGap = 26;
        int descW = juce::jlimit (220, 320, r.getWidth() / 4);
        auto layoutWidth = [&] (int knobW)
        {
            int total = 0;
            bool prevStack = false;
            juce::String lastG;
            bool first = true;
            for (auto& it : items)
            {
                const bool stack = it.control->isSwitch();
                const int w = stack ? it.w : (it.isChoice ? juce::jmax (knobW, 110) : knobW);
                if (! first) total += (it.group != lastG ? groupGap : (stack && prevStack ? 0 : gap));
                if (! (stack && prevStack)) total += w;
                prevStack = stack; lastG = it.group; first = false;
            }
            return total;
        };

        int knobW = 92;
        while (knobW > 66 && layoutWidth (knobW) > r.getWidth() - descW - 12) knobW -= 2;
        while (descW > 190 && layoutWidth (knobW) > r.getWidth() - descW - 12) descW -= 10;

        auto right = r.removeFromRight (descW);
        r.removeFromRight (12);
        descArea = right.removeFromTop (juce::jmax (60, right.getHeight() - 56));
        liveArea = right;
        controlsArea = r;

        headings.clear();
        bool anyGroup = false;
        for (auto& it : items) anyGroup = anyGroup || it.group.isNotEmpty();
        const int headH = anyGroup ? 18 : 0;
        int rowH = 0;
        for (auto& it : items) rowH = juce::jmax (rowH, it.h);
        const int y0 = r.getY() + headH + juce::jmax (0, (r.getHeight() - headH - rowH) / 2);

        int x = r.getX(), stackY = 0;
        bool prevStack = false;
        juce::String lastG;
        bool first = true;
        for (auto& it : items)
        {
            const bool stack = it.control->isSwitch();
            const int w = stack ? it.w : (it.isChoice ? juce::jmax (knobW, 110) : knobW);
            const bool newGroup = ! first && it.group != lastG;
            if (! first) x += newGroup ? groupGap : (stack && prevStack ? 0 : gap);

            if (it.group != lastG || first)
            {
                if (it.group.isNotEmpty())
                    headings.push_back ({ it.group, juce::Rectangle<int> (x, r.getY(), 220, 16) });
            }

            if (stack && prevStack) { stackY += it.h + 6; it.control->setBounds (x - it.w, y0 + stackY, it.w, it.h); }
            else { stackY = 0; it.control->setBounds (x, y0, w, it.h); }

            if (! (stack && prevStack)) x += w;
            prevStack = stack; lastG = it.group; first = false;
        }
    }

private:
    struct Item { std::unique_ptr<ParamControl> control; int w, h; juce::String group; bool isChoice = false; };
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

// Six-band mid/side EQ plus tilt. Pick a band, then its settings sit in one row.
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
            add (freq[(size_t) b], *spec (eqId (b, "freq")), "Frequency");
            add (gain[(size_t) b], *spec (eqId (b, "gain")), "Gain");
            add (q[(size_t) b], *spec (eqId (b, "q")), shelf ? "Slope" : "Q");
            add (target[(size_t) b], *spec (eqId (b, "tgt")), "Acts on");
            onAttach[(size_t) b] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, eqId (b, "on"), bandOn[(size_t) b]);
            bandOn[(size_t) b].setButtonText ("Band on");
            addChildComponent (bandOn[(size_t) b]);

            auto& sel = bandBtn[(size_t) b];
            sel.setButtonText (names[b]);
            sel.setClickingTogglesState (false);
            sel.onClick = [this, b] { proc.apvts.state.setProperty ("eqBand", b, nullptr); showBand(); };
            addAndMakeVisible (sel);
        }
        add (tilt, *spec ("eq_tilt"), "Tilt");
        tilt->setVisible (true);
        enable = std::make_unique<StageEnableToggle> (p.apvts, "en_eq");
        addAndMakeVisible (*enable);
        showBand();
    }

    void tick()
    {
        if (band() != shownBand) showBand();
        for (int b = 0; b < bm::dsp::EqParams::kBands; ++b)
        {
            bandBtn[(size_t) b].setColour (juce::TextButton::buttonOnColourId, pal().eqCol[b]);
            bandBtn[(size_t) b].setToggleState (b == shownBand, juce::dontSendNotification);
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, getLocalBounds().toFloat(), "Tonal EQ");
        g.setColour (p.textDim);
        g.setFont (font (10.5f, true));
        g.drawText (juce::String (names[shownBand]) + " SETTINGS", bandHeader, juce::Justification::centredLeft);
        g.drawText ("TILT", tiltHeader, juce::Justification::centredLeft);
        g.setColour (p.border);
        g.drawVerticalLine (divider, (float) bandHeader.getY(), (float) getHeight() - 14.0f);

        g.setColour (p.textDim);
        g.setFont (font (11.0f));
        g.drawFittedText ("Each band can act on the whole signal, the mid only or the side only. Each band has a numbered marker on the graph at its frequency: drag it to change frequency and gain. "
                          "Live mode = minimum phase, no latency. Broadcast mode = linear phase, adds latency. Tilt is a broad see-saw around 1 kHz.",
                          noteArea, juce::Justification::topLeft, 7, 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 8);
        auto title = r.removeFromTop (20);
        enable->setBounds (title.removeFromRight (110));
        r.removeFromTop (6);

        auto sel = r.removeFromTop (26);
        for (int b = 0; b < bm::dsp::EqParams::kBands; ++b)
        {
            bandBtn[(size_t) b].setBounds (sel.removeFromLeft (b == 0 || b == 5 ? 104 : 80));
            sel.removeFromLeft (6);
        }
        sel.removeFromLeft (10);
        for (auto& t : bandOn) t.setBounds (sel.getX(), sel.getY(), 100, 26);
        r.removeFromTop (8);

        const int w = 100, h = juce::jmin (108, r.getHeight() - 18);
        auto row = r.withTrimmedTop (18);
        bandHeader = r.withHeight (16).withWidth (w * 4);
        int x = r.getX();
        for (int b = 0; b < bm::dsp::EqParams::kBands; ++b)
        {
            int cx = x;
            for (auto* c : { freq[(size_t) b].get(), gain[(size_t) b].get(), q[(size_t) b].get() })
            { c->setBounds (cx, row.getY(), w, h); cx += w; }
            target[(size_t) b]->setBounds (cx + 4, row.getY(), 140, juce::jmin (h, 52));
        }
        x += w * 3 + 150;
        divider = x + 10;
        x += 24;
        tiltHeader = juce::Rectangle<int> (x, r.getY(), w, 16);
        tilt->setBounds (x, row.getY(), w, h);
        x += w + 20;
        noteArea = juce::Rectangle<int> (x, r.getY() + 4, juce::jmax (100, r.getRight() - x), r.getHeight() - 4);
    }

private:
    static constexpr const char* names[6] { "LOW SHELF", "BELL 1", "BELL 2", "BELL 3", "BELL 4", "HIGH SHELF" };

    void add (std::unique_ptr<ParamControl>& slot, const bm::params::ParamSpec& s, const juce::String& label)
    {
        slot = std::make_unique<ParamControl> (proc.apvts, s, ParamControl::Style::Knob, label);
        addChildComponent (*slot);
    }

    int band() const { return juce::jlimit (0, bm::dsp::EqParams::kBands - 1, (int) proc.apvts.state.getProperty ("eqBand", 0)); }

    void showBand()
    {
        shownBand = band();
        for (int b = 0; b < bm::dsp::EqParams::kBands; ++b)
        {
            const bool v = b == shownBand;
            for (auto* c : { freq[(size_t) b].get(), gain[(size_t) b].get(), q[(size_t) b].get(), target[(size_t) b].get() }) c->setVisible (v);
            bandOn[(size_t) b].setVisible (v);
        }
        repaint();
    }

    BroadcastMasterProcessor& proc;
    std::array<std::unique_ptr<ParamControl>, bm::dsp::EqParams::kBands> freq, gain, q, target;
    std::unique_ptr<ParamControl> tilt;
    std::array<juce::ToggleButton, bm::dsp::EqParams::kBands> bandOn;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, bm::dsp::EqParams::kBands> onAttach;
    std::array<juce::TextButton, bm::dsp::EqParams::kBands> bandBtn;
    std::unique_ptr<StageEnableToggle> enable;
    juce::Rectangle<int> bandHeader, tiltHeader, noteArea;
    int divider = 0, shownBand = -1;
};

// Four-band multiband compressor. The graph lives in the big spectrum window (crossovers and thresholds are
// dragged there); this panel holds the settings of the selected band in one row, plus the shared settings.
class MbcPanel : public juce::Component
{
public:
    explicit MbcPanel (BroadcastMasterProcessor& p) : proc (p)
    {
        using bm::params::mbId;
        auto spec = [] (const std::string& id) { return bm::params::findSpec (id); };
        for (int b = 0; b < bm::dsp::MbcParams::kBands; ++b)
        {
            add (thr[(size_t) b], *spec (mbId (b, "thr")), "Threshold");
            add (ratio[(size_t) b], *spec (mbId (b, "ratio")), "Ratio");
            add (atk[(size_t) b], *spec (mbId (b, "atk")), "Attack");
            add (rel[(size_t) b], *spec (mbId (b, "rel")), "Release");
            add (mk[(size_t) b], *spec (mbId (b, "mk")), "Makeup");
            on[(size_t) b] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, mbId (b, "on"), bandOn[(size_t) b]);
            bandOn[(size_t) b].setButtonText ("Band on");
            addChildComponent (bandOn[(size_t) b]);

            auto& sel = bandBtn[(size_t) b];
            sel.setButtonText (bandNames[b]);
            sel.setClickingTogglesState (false);
            sel.setColour (juce::TextButton::buttonOnColourId, pal().bandCol[b]);
            sel.onClick = [this, b] { selectBand (b); };
            addAndMakeVisible (sel);
        }
        soloBtn.setButtonText ("Solo band");
        soloBtn.setClickingTogglesState (false);
        soloBtn.setTooltip ("Listen to this band on the solo bus. Uses Monitor B and switches the solo bus on.");
        soloBtn.onClick = [this] { toggleSolo (band()); };
        addAndMakeVisible (soloBtn);

        add (x1, *spec ("mb_x1"), "Crossover 1");
        add (x2, *spec ("mb_x2"), "Crossover 2");
        add (x3, *spec ("mb_x3"), "Crossover 3");
        add (knee, *spec ("mb_knee"), "Knee");
        add (mix, *spec ("mb_mix"), "Mix");
        for (auto* c : { x1.get(), x2.get(), x3.get(), knee.get(), mix.get() }) c->setVisible (true);

        enable = std::make_unique<StageEnableToggle> (p.apvts, "en_mbc");
        addAndMakeVisible (*enable);
        showBand();
    }

    void tick()
    {
        if (band() != shownBand) showBand();
        for (int b = 0; b < bm::dsp::MbcParams::kBands; ++b)
        {
            bandBtn[(size_t) b].setColour (juce::TextButton::buttonOnColourId, pal().bandCol[b]);
            bandBtn[(size_t) b].setToggleState (b == shownBand, juce::dontSendNotification);
        }
        soloBtn.setToggleState (isSoloing (shownBand), juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, getLocalBounds().toFloat(), "Multiband Compressor");

        g.setColour (p.textDim);
        g.setFont (font (10.5f, true));
        g.drawText ((juce::String ("BAND ") + juce::String (shownBand + 1) + " SETTINGS").toUpperCase(), bandHeader, juce::Justification::centredLeft);
        g.drawText ("SHARED", sharedHeader, juce::Justification::centredLeft);

        g.setColour (p.border);
        g.drawVerticalLine (divider, (float) bandHeader.getY(), (float) getHeight() - 14.0f);

        g.setColour (p.textDim);
        g.setFont (font (11.0f));
        g.drawText ("Drag on the graph: crossovers sideways, thresholds up and down.", hint, juce::Justification::centredRight);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 8);
        auto title = r.removeFromTop (20);
        enable->setBounds (title.removeFromRight (110));
        hint = title.withTrimmedLeft (200).withTrimmedRight (16);
        r.removeFromTop (6);

        auto sel = r.removeFromTop (26);
        for (int b = 0; b < bm::dsp::MbcParams::kBands; ++b)
            bandBtn[(size_t) b].setBounds (sel.removeFromLeft (112)), sel.removeFromLeft (6);
        sel.removeFromLeft (10);
        for (auto& t : bandOn) t.setBounds (sel.getX(), sel.getY(), 100, 26);
        sel.removeFromLeft (110);
        soloBtn.setBounds (sel.removeFromLeft (100));
        r.removeFromTop (8);

        // one row: five band knobs | shared knobs
        const int total = 10;
        const int gapDiv = 30;
        const int w = juce::jmin (100, (r.getWidth() - gapDiv) / total);
        const int h = juce::jmin (108, r.getHeight() - 18);
        auto row = r.withTrimmedTop (18);
        bandHeader = r.withHeight (16).withWidth (w * 5);
        int x = r.getX();
        for (int b = 0; b < bm::dsp::MbcParams::kBands; ++b)
        {
            int cx = x;
            for (auto* c : { thr[(size_t) b].get(), ratio[(size_t) b].get(), atk[(size_t) b].get(), rel[(size_t) b].get(), mk[(size_t) b].get() })
            {
                c->setBounds (cx, row.getY(), w, h);
                cx += w;
            }
        }
        x += w * 5 + gapDiv / 2;
        divider = x;
        x += gapDiv / 2;
        sharedHeader = juce::Rectangle<int> (x, r.getY(), w * 5, 16);
        for (auto* c : { x1.get(), x2.get(), x3.get(), knee.get(), mix.get() })
        {
            c->setBounds (x, row.getY(), w, h);
            x += w;
        }
    }

private:
    static constexpr const char* bandNames[4] { "1  LOW", "2  LOW-MID", "3  HIGH-MID", "4  HIGH" };

    void add (std::unique_ptr<ParamControl>& slot, const bm::params::ParamSpec& s, const juce::String& label)
    {
        slot = std::make_unique<ParamControl> (proc.apvts, s, ParamControl::Style::Knob, label);
        addChildComponent (*slot);
    }

    int band() const { return juce::jlimit (0, 3, (int) proc.apvts.state.getProperty ("mbcBand", 0)); }

    void selectBand (int b) { proc.apvts.state.setProperty ("mbcBand", b, nullptr); showBand(); }

    void showBand()
    {
        shownBand = band();
        for (int b = 0; b < bm::dsp::MbcParams::kBands; ++b)
        {
            const bool v = b == shownBand;
            for (auto* c : { thr[(size_t) b].get(), ratio[(size_t) b].get(), atk[(size_t) b].get(), rel[(size_t) b].get(), mk[(size_t) b].get() })
                c->setVisible (v);
            bandOn[(size_t) b].setVisible (v);
        }
        repaint();
    }

    float raw (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }

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
    std::array<std::unique_ptr<ParamControl>, 4> thr, ratio, atk, rel, mk;
    std::unique_ptr<ParamControl> x1, x2, x3, knee, mix;
    std::array<juce::ToggleButton, 4> bandOn;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 4> on;
    std::array<juce::TextButton, 4> bandBtn;
    juce::TextButton soloBtn;
    std::unique_ptr<StageEnableToggle> enable;
    juce::Rectangle<int> bandHeader, sharedHeader, hint;
    int divider = 0, shownBand = -1;
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
