// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki
#pragma once
#include <array>
#include <atomic>
#include <cstring>
#include <vector>
#include "AutoLeveler.h"
#include "MultibandComp.h"
#include "SimpleModules.h"
#include "TonalEq.h"
#include "Tonality.h"
#include "TruePeakLimiter.h"
#include "../Meters/MeterBank.h"

// The whole processing chain without any plugin-framework code, so it can be tested with plain C++.
namespace bm::dsp
{
inline constexpr int kStageCount = 12;
// Stage order is the signal order, left to right: clean-up, tone, dynamics, colour, stereo, loudness, then peak control.
enum Stage { sInput = 0, sHpf, sPhase, sEq, sMbc, sSat, sExc, sStereo, sLev, sClip, sLim, sOut };

// What the solo / monitor bus plays.
enum class MonSrc { Output = 0, Input, Delta, LimiterDelta, Band1, Band2, Band3, Band4 };

struct ChainParams
{
    float gainDb[kStageCount] { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1.0f, 0 }; // stage output gains; [sLim] is the ceiling
    bool on[kStageCount] { true, true, false, true, false, false, false, true, false, false, true, true };
    bool broadcast = false;                 // false = Live (low latency), true = Broadcast (linear-phase EQ, longer look-ahead)

    float hpFreq = 25.0f, phaseFreq = 160.0f;
    int phaseStages = 2;
    EqParams eq;
    SatType satType = SatType::Tube;
    float satAmount = 25.0f, satMix = 100.0f;
    float excFreq = 4500.0f, excAmount = 20.0f, bassFreq = 80.0f, bassAmount = 20.0f;
    MbcParams mbc;
    LevParams lev;
    StereoUtils::Params stereo;
    float clipThreshold = -2.0f, clipKnee = 50.0f;
    float limRelease = 100.0f;

    bool bypass = false, matchBypass = true;
    bool soloOn = false;
    int monSrc = 0;
    float monLevelDb = 0.0f;
};

class Chain
{
public:
    static constexpr int kMaxChunk = 1024;
    static constexpr float kLiveLookaheadMs = 1.0f, kBroadcastLookaheadMs = 5.0f;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        hpf.prepare (fs); phase.prepare (fs); eq.prepare (fs); sat.prepare (fs); enh.prepare (fs);
        mbc.prepare (fs); lev.prepare (fs); stereo.prepare (fs); clip.prepare (fs); lim.prepare (fs);
        wetTrack.prepare (fs); dryTrack.prepare (fs);

        rampLen = std::max (1, (int) (0.02 * fs));
        fadeStep = 1.0f / (float) rampLen;

        const size_t cap = (size_t) (0.5 * fs) + 16384;
        for (auto& d : dryRing) d.assign (cap, 0.0f);
        for (auto* v : { &dryL, &dryR, &fadeL, &fadeR, &monL, &monR, &bandL, &bandR, &limDL, &limDR, &procL, &procR })
            v->assign ((size_t) kMaxChunk, 0.0f);

        broadcast = false;
        eq.setLinearPhase (false);
        lim.setParams (-1.0f, 100.0f, kLiveLookaheadMs, true);
        reset();
    }

    void reset()
    {
        hpf.reset(); phase.reset(); eq.reset(); sat.reset(); enh.reset(); mbc.reset(); lev.reset();
        stereo.resetState(); clip.reset(); lim.reset();
        wetTrack.reset(); dryTrack.reset();
        for (auto& d : dryRing) std::fill (d.begin(), d.end(), 0.0f);
        dryWp = 0;
        for (auto& r : ramp) r = Ramp {};
        for (auto& f : fades) f = Fade {};
        bypassMix = 0.0f;
        matchOffsetDbNow = 0.0f; matchTargetDb = 0.0f; matchGainNow = 1.0f;
    }

    // Samples of delay the chain adds. Fixed by mode (and sample rate): enabling or disabling a module never changes it.
    int latency() const noexcept { return (broadcast ? kEqLatency : 0) + lim.latency(); }
    bool isBroadcast() const noexcept { return broadcast; }
    void applyMode (bool b) { if (b != broadcast) setMode (b); }
    double sampleRate() const noexcept { return fs; }

    // Panic: clears the leveler gain and the limiter, back to a clean start (latency does not change).
    void panic() noexcept { lev.reset(); lim.reset(); }

    // True for NaN and infinity. Tested on the bit pattern, so it keeps working under fast-math compiler options.
    static bool isBad (float x) noexcept
    {
        std::uint32_t b; std::memcpy (&b, &x, sizeof b);
        return (b & 0x7f800000u) == 0x7f800000u;
    }

    // Processes n samples in place. auxL/auxR (optional) receive the solo bus (silence when solo is off).
    // Without an aux bus and with soloToMain set, the solo signal replaces the main output.
    void process (const ChainParams& p, float* l, float* r, int n, float* auxL, float* auxR, bool soloToMain,
                  bm::MeterBank* mIn, bm::MeterBank* mOut) noexcept
    {
        // One NaN or infinite sample from upstream would otherwise stay in the filters for good: replace them with silence.
        for (int i = 0; i < n; ++i)
        {
            if (isBad (l[i])) l[i] = 0.0f;
            if (isBad (r[i])) r[i] = 0.0f;
        }

        for (int off = 0; off < n; off += kMaxChunk)
        {
            const int k = std::min (kMaxChunk, n - off);
            processChunk (p, l + off, r + off, k, auxL ? auxL + off : nullptr, auxR ? auxR + off : nullptr, soloToMain, mIn, mOut);
        }

        // Safety net: if anything non-finite still came out, mute this block and start the chain clean.
        bool bad = false;
        for (int i = 0; i < n && ! bad; ++i) bad = isBad (l[i]) || isBad (r[i]);
        if (bad)
        {
            std::fill (l, l + n, 0.0f); std::fill (r, r + n, 0.0f);
            if (auxL) std::fill (auxL, auxL + n, 0.0f);
            if (auxR) std::fill (auxR, auxR + n, 0.0f);
            reset();
        }
    }

    // --- read by the UI and tests ------------------------------------------------------------------------
    float stagePeak (int s) const noexcept { return stagePeakDb[(size_t) s].load (std::memory_order_relaxed); }
    float stageRms (int s) const noexcept  { return stageRmsDb[(size_t) s].load (std::memory_order_relaxed); }
    float matchOffsetDb() const noexcept { return matchOffsetOut.load (std::memory_order_relaxed); }

    SubsonicHpf hpf;
    PhaseRotator phase;
    TonalEq eq;
    Saturator sat;
    ToneEnhancer enh;
    MultibandComp mbc;
    AutoLeveler lev;
    StereoUtils stereo;
    SoftClipper clip;
    TruePeakLimiter lim;

private:
    // Linear gain ramp, so stage gains and switches never click.
    struct Ramp
    {
        float cur = 1.0f, target = 1.0f, step = 0.0f;
        int remain = 0;
        bool started = false;
        void set (float t, int len) noexcept
        {
            if (! started) { started = true; cur = target = t; remain = 0; return; } // first value: no ramp from unity
            if (t == target) return;
            target = t;
            remain = std::max (1, len);
            step = (t - cur) / (float) remain;
        }
        void apply (float* l, float* r, int n) noexcept
        {
            if (remain == 0 && cur == 1.0f) return;
            for (int i = 0; i < n; ++i)
            {
                if (remain > 0) { cur += step; if (--remain == 0) cur = target; }
                l[i] *= cur;
                r[i] *= cur;
            }
        }
    };

    struct Fade { float mix = 0.0f; bool wasReset = true, started = false; };

    // Loudness of a stereo signal over a couple of seconds (K-weighted energy average).
    struct LoudTracker
    {
        bm::KWeighting kw[2];
        double e = 0.0, c = 0.0;
        void prepare (double fs) { kw[0].prepare (fs); kw[1].prepare (fs); c = 1.0 - std::exp (-1.0 / (2.0 * fs)); reset(); }
        void reset() noexcept { kw[0].reset(); kw[1].reset(); e = 0.0; }
        void process (const float* l, const float* r, int n) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                const double a = kw[0].process (l[i]), b = kw[1].process (r[i]);
                e += c * (a * a + b * b - e);
            }
        }
        double lufs() const noexcept { return e > 1.0e-12 ? -0.691 + 10.0 * std::log10 (e) : -120.0; }
    };

    // Runs a module with a crossfade when it is switched on or off. A module that is fully off is not run
    // (and is reset once, so it starts clean when switched back on).
    template <class F, class R>
    void fade (int s, bool on, float* l, float* r, int n, F&& run, R&& resetModule) noexcept
    {
        auto& f = fades[(size_t) s];
        const float target = on ? 1.0f : 0.0f;
        if (! f.started) { f.started = true; f.mix = target; } // first block: no fade-in from nothing

        if (f.mix == 0.0f && target == 0.0f)
        {
            if (! f.wasReset) { resetModule(); f.wasReset = true; }
            return;
        }
        f.wasReset = false;

        if (f.mix == 1.0f && target == 1.0f) { run (l, r, n); return; }

        std::memcpy (fadeL.data(), l, (size_t) n * sizeof (float));
        std::memcpy (fadeR.data(), r, (size_t) n * sizeof (float));
        run (l, r, n);
        for (int i = 0; i < n; ++i)
        {
            if (f.mix < target)      f.mix = std::min (target, f.mix + fadeStep);
            else if (f.mix > target) f.mix = std::max (target, f.mix - fadeStep);
            l[i] = fadeL[(size_t) i] + (l[i] - fadeL[(size_t) i]) * f.mix;
            r[i] = fadeR[(size_t) i] + (r[i] - fadeR[(size_t) i]) * f.mix;
        }
    }

    void measure (int s, const float* l, const float* r, int n) noexcept
    {
        float peak = 0.0f;
        double sumSq = 0.0;
        for (int i = 0; i < n; ++i)
        {
            peak = std::max ({ peak, std::fabs (l[i]), std::fabs (r[i]) });
            sumSq += (double) l[i] * l[i] + (double) r[i] * r[i];
        }
        stagePeakDb[(size_t) s].store (bm::toDb (peak), std::memory_order_relaxed);
        stageRmsDb[(size_t) s].store ((float) bm::toDb (std::sqrt (sumSq / (2.0 * (double) std::max (1, n)))), std::memory_order_relaxed);
    }

    void gainStage (const ChainParams& p, int s, bool active, float* l, float* r, int n) noexcept
    {
        float db = active ? p.gainDb[s] : 0.0f;
        if (s == sOut) db = std::min (0.0f, db); // output trim only cuts
        ramp[(size_t) s].set (dbToLin (db), rampLen);
        ramp[(size_t) s].apply (l, r, n);
        measure (s, l, r, n);
    }

    void setMode (bool b)
    {
        broadcast = b;
        eq.setLinearPhase (b);
        lim.setParams (-1.0f, 100.0f, b ? kBroadcastLookaheadMs : kLiveLookaheadMs, true); // resets the limiter on a length change
    }

    void processChunk (const ChainParams& p, float* l, float* r, int n, float* auxL, float* auxR, bool soloToMain,
                       bm::MeterBank* mIn, bm::MeterBank* mOut) noexcept
    {
        if (p.broadcast != broadcast) setMode (p.broadcast);

        // The untouched input, delayed to line up with the processed signal (chain latency + lip-sync delay).
        {
            const size_t cap = dryRing[0].size();
            const size_t d = (size_t) std::min ((int) cap - 1, latency() + stereo.currentDelaySamples());
            for (int i = 0; i < n; ++i)
            {
                dryRing[0][dryWp] = l[i];
                dryRing[1][dryWp] = r[i];
                const size_t rp = (dryWp + cap - d) % cap;
                dryL[(size_t) i] = dryRing[0][rp];
                dryR[(size_t) i] = dryRing[1][rp];
                dryWp = (dryWp + 1) % cap;
            }
        }

        if (mIn) mIn->process (l, r, n);

        const bool soloBand = p.soloOn && p.monSrc >= (int) MonSrc::Band1;
        const bool needLimDelta = p.soloOn && p.monSrc == (int) MonSrc::LimiterDelta;
        const bool needDelta = p.soloOn && p.monSrc == (int) MonSrc::Delta;
        if (soloBand)
        {
            std::fill (bandL.begin(), bandL.begin() + n, 0.0f);
            std::fill (bandR.begin(), bandR.begin() + n, 0.0f);
        }
        if (needLimDelta)
        {
            std::fill (limDL.begin(), limDL.begin() + n, 0.0f);
            std::fill (limDR.begin(), limDR.begin() + n, 0.0f);
        }

        // 0 input trim
        gainStage (p, sInput, true, l, r, n);

        // 1 subsonic high-pass
        fade (sHpf, p.on[sHpf], l, r, n,
              [&] (float* a, float* b, int k) { hpf.setFrequency (p.hpFreq); hpf.process (a, b, k); },
              [&] { hpf.reset(); });
        gainStage (p, sHpf, p.on[sHpf], l, r, n);

        // 2 phase rotation
        fade (sPhase, p.on[sPhase], l, r, n,
              [&] (float* a, float* b, int k) { phase.setParams (p.phaseFreq, p.phaseStages); phase.process (a, b, k); },
              [&] { phase.reset(); });
        gainStage (p, sPhase, p.on[sPhase], l, r, n);

        // 3 tonal EQ (always runs: in Broadcast mode a disabled EQ is a flat-kernel delay so latency stays fixed)
        {
            EqParams ep = p.eq;
            ep.enabled = p.on[sEq];
            eq.setParams (ep);
            eq.process (l, r, n);
            gainStage (p, sEq, p.on[sEq], l, r, n);
        }

        // 4 multiband compressor
        fade (sMbc, p.on[sMbc], l, r, n,
              [&] (float* a, float* b, int k)
              {
                  mbc.setParams (p.mbc);
                  if (soloBand) mbc.process (a, b, k, p.monSrc - (int) MonSrc::Band1, bandL.data(), bandR.data());
                  else          mbc.process (a, b, k);
              },
              [&] { mbc.reset(); });
        gainStage (p, sMbc, p.on[sMbc], l, r, n);

        // 5 saturation
        fade (sSat, p.on[sSat], l, r, n,
              [&] (float* a, float* b, int k) { sat.setParams (p.satType, p.satAmount, p.satMix); sat.process (a, b, k); },
              [&] { sat.reset(); });
        gainStage (p, sSat, p.on[sSat], l, r, n);

        // 6 exciter and bass enhancer
        fade (sExc, p.on[sExc], l, r, n,
              [&] (float* a, float* b, int k)
              {
                  enh.setParams (p.excFreq, p.excAmount, p.bassFreq, p.bassAmount);
                  enh.process (a, b, k);
              },
              [&] { enh.reset(); });
        gainStage (p, sExc, p.on[sExc], l, r, n);

        // 7 stereo utilities (neutral when switched off; the lip-sync delay crossfades itself)
        {
            stereo.setParams (p.on[sStereo] ? p.stereo : StereoUtils::Params {});
            stereo.process (l, r, n);
            gainStage (p, sStereo, p.on[sStereo], l, r, n);
        }

        // 8 auto leveler
        fade (sLev, p.on[sLev], l, r, n,
              [&] (float* a, float* b, int k) { lev.setParams (p.lev); lev.process (a, b, k); },
              [&] { lev.reset(); });
        gainStage (p, sLev, p.on[sLev], l, r, n);

        // 9 soft clipper
        fade (sClip, p.on[sClip], l, r, n,
              [&] (float* a, float* b, int k) { clip.setParams (p.clipThreshold, p.clipKnee); clip.process (a, b, k); },
              [&] { clip.reset(); });
        gainStage (p, sClip, p.on[sClip], l, r, n);

        // 10 true-peak limiter (its "gain" is the ceiling; a disabled limiter is a plain delay)
        lim.setParams (std::min (0.0f, p.gainDb[sLim]), p.limRelease, broadcast ? kBroadcastLookaheadMs : kLiveLookaheadMs, p.on[sLim]);
        lim.process (l, r, n, needLimDelta ? limDL.data() : nullptr, needLimDelta ? limDR.data() : nullptr);
        measure (sLim, l, r, n);

        // 11 output trim
        gainStage (p, sOut, true, l, r, n);

        // loudness-matched A/B bypass
        if (needDelta)
        {
            std::memcpy (procL.data(), l, (size_t) n * sizeof (float));
            std::memcpy (procR.data(), r, (size_t) n * sizeof (float));
        }

        wetTrack.process (l, r, n);
        dryTrack.process (dryL.data(), dryR.data(), n);
        {
            const double w = wetTrack.lufs(), d = dryTrack.lufs();
            if (w > -60.0 && d > -60.0) matchTargetDb = (float) std::clamp (w - d, -12.0, 12.0);
            const float c = 1.0f - std::exp (-(float) n / (float) fs / 1.0f); // about one second
            matchOffsetDbNow += (matchTargetDb - matchOffsetDbNow) * c;
            matchOffsetOut.store (p.matchBypass ? matchOffsetDbNow : 0.0f, std::memory_order_relaxed);
        }
        {
            const float gNew = p.matchBypass ? dbToLin (matchOffsetDbNow) : 1.0f;
            const float bTarget = p.bypass ? 1.0f : 0.0f;
            const float gStep = (gNew - matchGainNow) / (float) n;
            if (bypassMix == bTarget && bTarget == 0.0f)
                matchGainNow = gNew;
            else
                for (int i = 0; i < n; ++i)
                {
                    if (bypassMix < bTarget)      bypassMix = std::min (bTarget, bypassMix + fadeStep);
                    else if (bypassMix > bTarget) bypassMix = std::max (bTarget, bypassMix - fadeStep);
                    matchGainNow += gStep;
                    l[i] += (dryL[(size_t) i] * matchGainNow - l[i]) * bypassMix;
                    r[i] += (dryR[(size_t) i] * matchGainNow - r[i]) * bypassMix;
                }
            matchGainNow = gNew;
        }

        if (mOut) mOut->process (l, r, n);

        // solo / monitor bus
        const bool wantMon = p.soloOn && (auxL || soloToMain);
        if (wantMon)
        {
            const float g = dbToLin (p.monLevelDb);
            for (int i = 0; i < n; ++i)
            {
                float a = 0.0f, b = 0.0f;
                switch ((MonSrc) p.monSrc)
                {
                    case MonSrc::Output:       a = l[i]; b = r[i]; break;
                    case MonSrc::Input:        a = dryL[(size_t) i]; b = dryR[(size_t) i]; break;
                    case MonSrc::Delta:        a = procL[(size_t) i] - dryL[(size_t) i]; b = procR[(size_t) i] - dryR[(size_t) i]; break;
                    case MonSrc::LimiterDelta: a = limDL[(size_t) i]; b = limDR[(size_t) i]; break;
                    default:                   a = bandL[(size_t) i]; b = bandR[(size_t) i]; break;
                }
                monL[(size_t) i] = a * g;
                monR[(size_t) i] = b * g;
            }
        }

        if (auxL && auxR)
        {
            if (wantMon)
            {
                std::memcpy (auxL, monL.data(), (size_t) n * sizeof (float));
                std::memcpy (auxR, monR.data(), (size_t) n * sizeof (float));
            }
            else
            {
                std::fill (auxL, auxL + n, 0.0f);
                std::fill (auxR, auxR + n, 0.0f);
            }
        }
        else if (wantMon && soloToMain)
        {
            std::memcpy (l, monL.data(), (size_t) n * sizeof (float));
            std::memcpy (r, monR.data(), (size_t) n * sizeof (float));
        }
    }

    double fs = 48000.0;
    bool broadcast = false;
    int rampLen = 960;
    float fadeStep = 0.001f;

    std::array<Ramp, kStageCount> ramp {};
    std::array<Fade, kStageCount> fades {};

    std::vector<float> dryRing[2];
    size_t dryWp = 0;
    std::vector<float> dryL, dryR, fadeL, fadeR, monL, monR, bandL, bandR, limDL, limDR, procL, procR;

    LoudTracker wetTrack, dryTrack;
    float bypassMix = 0.0f, matchOffsetDbNow = 0.0f, matchTargetDb = 0.0f, matchGainNow = 1.0f;
    std::atomic<float> matchOffsetOut { 0.0f };

    std::array<std::atomic<float>, kStageCount> stagePeakDb, stageRmsDb;

public:
    Chain()
    {
        for (auto& a : stagePeakDb) a.store (-120.0f);
        for (auto& a : stageRmsDb) a.store (-120.0f);
    }
};
} // namespace bm::dsp
