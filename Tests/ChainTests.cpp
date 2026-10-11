// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki
// Tests for the assembled processing chain (no JUCE needed).
// Build: g++ -std=c++17 -O2 -Wall -Wextra -ISource Tests/ChainTests.cpp -o chain_tests && ./chain_tests
#include <cstdio>
#include <random>
#include <vector>
#include "Dsp/Chain.h"

using namespace bm::dsp;

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL line %d: ", __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

constexpr double FS = 48000.0;
using Buf = std::vector<float>;

static Buf noise (int n, float amp, unsigned seed)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> nd (0.0f, 1.0f);
    Buf b ((size_t) n);
    for (auto& v : b) v = amp * nd (rng);
    return b;
}

static Buf sine (double f, double amp, int n)
{
    Buf b ((size_t) n);
    for (int i = 0; i < n; ++i) b[(size_t) i] = (float) (amp * std::sin (2.0 * bm::kPi * f * i / FS));
    return b;
}

// Runs a stereo signal through the chain with the given host block size. Returns main outputs and aux outputs.
struct Result { Buf l, r, auxL, auxR; };
static Result run (Chain& c, const ChainParams& p, const Buf& inL, const Buf& inR, int block, bool aux = false, bool soloToMain = false)
{
    Result res { inL, inR, Buf (inL.size(), 0.0f), Buf (inL.size(), 0.0f) };
    for (size_t i = 0; i < inL.size(); i += (size_t) block)
    {
        const int k = (int) std::min<size_t> ((size_t) block, inL.size() - i);
        c.process (p, res.l.data() + i, res.r.data() + i, k, aux ? res.auxL.data() + i : nullptr, aux ? res.auxR.data() + i : nullptr, soloToMain, nullptr, nullptr);
    }
    return res;
}

static ChainParams plain()
{
    ChainParams p;
    for (bool& b : p.on) b = false;
    p.on[sInput] = p.on[sOut] = true;
    return p;
}

static int peakIndex (const Buf& b)
{
    int idx = 0; float m = 0;
    for (size_t i = 0; i < b.size(); ++i) if (std::fabs (b[i]) > m) { m = std::fabs (b[i]); idx = (int) i; }
    return idx;
}

static void testLatency()
{
    for (bool bc : { false, true })
        for (bool eqOn : { false, true })
            for (bool limOn : { false, true })
            {
                Chain c; c.prepare (FS);
                ChainParams p = plain();
                p.broadcast = bc; p.on[sEq] = eqOn; p.on[sLim] = limOn;
                Buf l (20000, 0.0f), r (20000, 0.0f); l[2000] = 0.1f; r[2000] = 0.1f;
                // flat EQ in Broadcast mode needs its kernel: the flat default kernel is already installed
                auto out = run (c, p, l, r, 500);
                const int lat = c.latency();
                const int expected = (bc ? kEqLatency : 0) + c.lim.latency();
                CHECK (lat == expected, "latency formula");
                CHECK (peakIndex (out.l) == 2000 + lat, "impulse at latency (bc=%d eq=%d lim=%d): peak at %d want %d", bc, eqOn, limOn, peakIndex (out.l) - 2000, lat);
                CHECK (std::fabs (out.l[(size_t) (2000 + lat)] - 0.1f) < 2e-3f, "impulse level %f", out.l[(size_t) (2000 + lat)]);
            }

    Chain a, b; a.prepare (FS); b.prepare (FS);
    ChainParams p = plain(); p.broadcast = false;
    ChainParams q = p; q.broadcast = true;
    Buf z (4800, 0.0f);
    run (a, p, z, z, 480); run (b, q, z, z, 480);
    CHECK (b.latency() > a.latency(), "Broadcast latency %d > Live %d", b.latency(), a.latency());
    std::printf ("  latency at %.1f kHz: Live %d samples (%.1f ms), Broadcast %d samples (%.1f ms)\n", FS / 1000.0,
                 a.latency(), 1000.0 * a.latency() / FS, b.latency(), 1000.0 * b.latency() / FS);
}

static void testBlockIndependence()
{
    ChainParams p;                     // defaults: HPF, EQ, stereo, limiter on
    p.on[sSat] = true; p.on[sMbc] = true; p.on[sClip] = true;
    p.eq.band[2].gainDb = 3.0f;
    p.mbc.band[1].thrDb = -30.0f;
    const Buf l = noise (96000, 0.3f, 1), r = noise (96000, 0.3f, 2);
    Chain a, b; a.prepare (FS); b.prepare (FS);
    auto o1 = run (a, p, l, r, 64), o2 = run (b, p, l, r, 3000);
    double d = 0;
    for (size_t i = 0; i < l.size(); ++i) d = std::max (d, (double) std::fabs (o1.l[i] - o2.l[i]));
    CHECK (d < 2e-5, "block size independence, max diff %g", d);
}

static double lufs (const Buf& l, const Buf& r, int from)
{
    bm::KWeighting kl, kr; kl.prepare (FS); kr.prepare (FS);
    double e = 0; int n = 0;
    for (size_t i = (size_t) from; i < l.size(); ++i) { const double a = kl.process (l[i]), b = kr.process (r[i]); e += a * a + b * b; ++n; }
    return -0.691 + 10.0 * std::log10 (e / n);
}

static void testBypass()
{
    // exact alignment with matching off
    {
        ChainParams p; p.bypass = true; p.matchBypass = false; p.gainDb[sInput] = 6.0f; p.on[sEq] = true; p.on[sSat] = true; p.satAmount = 80;
        Chain c; c.prepare (FS);
        const Buf l = noise (48000, 0.2f, 3), r = noise (48000, 0.2f, 4);
        auto o = run (c, p, l, r, 256);
        const int lat = c.latency();
        double d = 0;
        for (size_t i = 24000; i < l.size(); ++i) d = std::max (d, (double) std::fabs (o.l[i] - l[i - (size_t) lat]));
        CHECK (d < 1e-6, "bypass is the input delayed by the latency, diff %g", d);
    }
    // loudness match: processed is +6 dB louder; bypass should match it within a dB after settling
    {
        ChainParams p; p.gainDb[sInput] = 6.0f; p.matchBypass = true;
        Chain c; c.prepare (FS);
        const Buf l = noise ((int) FS * 12, 0.05f, 5), r = noise ((int) FS * 12, 0.05f, 6);
        auto wet = run (c, p, l, r, 512);
        p.bypass = true;
        auto byp = run (c, p, l, r, 512);
        const int from = (int) FS * 8;
        const double lw = lufs (wet.l, wet.r, from), lb = lufs (byp.l, byp.r, from);
        CHECK (std::fabs (lw - lb) < 1.0, "matched bypass loudness: processed %.2f, bypassed %.2f", lw, lb);
        CHECK (std::fabs (c.matchOffsetDb() - 6.0f) < 1.0f, "match offset %f", c.matchOffsetDb());
        p.matchBypass = false;
        Chain c2; c2.prepare (FS);
        auto byp2 = run (c2, p, l, r, 512);
        CHECK (lufs (byp2.l, byp2.r, from) < lw - 4.0, "unmatched bypass is quieter");
    }
}

static void testSolo()
{
    const Buf l = noise (48000, 0.08f, 7), r = noise (48000, 0.08f, 8);
    ChainParams p = plain(); p.on[sLim] = true; p.gainDb[sInput] = 3.0f;

    {   // solo off: aux is silent
        Chain c; c.prepare (FS);
        auto o = run (c, p, l, r, 480, true);
        double m = 0; for (float v : o.auxL) m = std::max (m, (double) std::fabs (v));
        CHECK (m == 0.0, "aux silent when solo is off");
    }
    {   // input cue = dry aligned
        Chain c; c.prepare (FS);
        p.soloOn = true; p.monSrc = (int) MonSrc::Input;
        auto o = run (c, p, l, r, 480, true);
        const int lat = c.latency();
        double d = 0; for (size_t i = 2000; i < l.size(); ++i) d = std::max (d, (double) std::fabs (o.auxL[i] - l[i - (size_t) lat]));
        CHECK (d < 1e-6, "input cue aligned, diff %g", d);
    }
    {   // delta = processed - dry (input trim +3 dB, limiter transparent)
        Chain c; c.prepare (FS);
        p.monSrc = (int) MonSrc::Delta;
        auto o = run (c, p, l, r, 480, true);
        const int lat = c.latency();
        const float g = dbToLin (3.0f) - 1.0f;
        double d = 0; for (size_t i = 2000; i < l.size(); ++i) d = std::max (d, (double) std::fabs (o.auxL[i] - g * l[i - (size_t) lat]));
        CHECK (d < 1e-4, "delta listen, diff %g", d);
    }
    {   // output cue equals main out; level control
        Chain c; c.prepare (FS);
        p.monSrc = (int) MonSrc::Output; p.monLevelDb = -6.0206f;
        auto o = run (c, p, l, r, 480, true);
        double d = 0; for (size_t i = 0; i < l.size(); ++i) d = std::max (d, (double) std::fabs (o.auxL[i] - 0.5f * o.l[i]));
        CHECK (d < 1e-5, "output cue with -6 dB monitor level, diff %g", d);
    }
    {   // MBC band solo carries the band
        Chain c; c.prepare (FS);
        ChainParams q = plain(); q.on[sMbc] = true; for (auto& b : q.mbc.band) b.on = false;
        q.soloOn = true; q.monSrc = (int) MonSrc::Band1;
        auto lo = sine (60, 0.3, 48000);
        auto o = run (c, q, lo, lo, 480, true);
        double m = 0; for (size_t i = 24000; i < lo.size(); ++i) m = std::max (m, (double) std::fabs (o.auxL[i]));
        CHECK (m > 0.2, "band 1 solo carries 60 Hz, peak %f", m);
        q.monSrc = (int) MonSrc::Band4;
        Chain c2; c2.prepare (FS);
        auto o2 = run (c2, q, lo, lo, 480, true);
        double m2 = 0; for (size_t i = 24000; i < lo.size(); ++i) m2 = std::max (m2, (double) std::fabs (o2.auxL[i]));
        CHECK (m2 < 0.01, "band 4 solo does not, peak %f", m2);
    }
    {   // limiter delta is non-zero only when the limiter works
        Chain c; c.prepare (FS);
        ChainParams q = plain(); q.on[sLim] = true; q.gainDb[sLim] = -6.0f;
        q.soloOn = true; q.monSrc = (int) MonSrc::LimiterDelta;
        auto loud = sine (300, 0.9, 48000);
        auto o = run (c, q, loud, loud, 480, true);
        double m = 0; for (size_t i = 24000; i < loud.size(); ++i) m = std::max (m, (double) std::fabs (o.auxL[i]));
        CHECK (m > 0.1, "limiter delta audible, peak %f", m);
    }
    {   // solo to main replaces the main output; without the flag the main is untouched
        Chain c; c.prepare (FS);
        ChainParams q = plain(); q.soloOn = true; q.monSrc = (int) MonSrc::Input; q.gainDb[sInput] = 6.0f;
        auto o = run (c, q, l, r, 480, false, true);
        const int lat = c.latency();
        double d = 0; for (size_t i = 2000; i < l.size(); ++i) d = std::max (d, (double) std::fabs (o.l[i] - l[i - (size_t) lat]));
        CHECK (d < 1e-6, "solo-to-main plays the input cue, diff %g", d);
        Chain c2; c2.prepare (FS);
        auto o2 = run (c2, q, l, r, 480, false, false);
        double d2 = 0; for (size_t i = 2000; i < l.size(); ++i) d2 = std::max (d2, (double) std::fabs (o2.l[i] - dbToLin (6.0f) * l[i - (size_t) lat]));
        CHECK (d2 < 1e-3, "no aux and fallback off leaves main alone, diff %g", d2);
    }
}

static void testEnableClick()
{
    // switch saturation, MBC and clipper on in the middle of a sine: no jump bigger than the sine's own slope allows
    Chain c; c.prepare (FS);
    ChainParams p = plain(); p.on[sLim] = false;
    const Buf s = sine (1000, 0.5, 48000);
    Buf l = s, r = s;
    auto step = [&] (size_t from, size_t to)
    {
        c.process (p, l.data() + from, r.data() + from, (int) (to - from), nullptr, nullptr, false, nullptr, nullptr);
    };
    step (0, 24000);
    p.on[sSat] = true; p.satAmount = 60; p.on[sMbc] = true; p.mbc.band[2].thrDb = -30.0f; p.on[sClip] = true; p.clipThreshold = -6.0f;
    step (24000, 48000);
    double maxJump = 0;
    for (size_t i = 24001; i < 36000; ++i) maxJump = std::max (maxJump, (double) std::fabs (l[i] - l[i - 1]));
    CHECK (maxJump < 0.5 * 2.0 * bm::kPi * 1000 / FS * 2.0, "no click when modules switch on: max step %f", maxJump);
    for (float v : l) CHECK (std::isfinite (v), "finite");
}

static void testAllOn()
{
    for (bool bc : { false, true })
    {
        Chain c; c.prepare (FS);
        ChainParams p;
        for (bool& b : p.on) b = true;
        p.broadcast = bc;
        p.gainDb[sInput] = 6.0f;
        p.gainDb[sLim] = -1.0f;
        p.gainDb[sOut] = -3.0f;
        p.eq.band[0].gainDb = 4.0f; p.eq.band[2].gainDb = -3.0f; p.eq.band[4].gainDb = 5.0f; p.eq.tiltDb = 2.0f;
        p.satAmount = 70; p.excAmount = 50; p.bassAmount = 50;
        p.mbc.band[0].thrDb = -30.0f; p.mbc.band[3].thrDb = -28.0f;
        p.lev.targetLufs = -16.0f; p.lev.mode = LevMode::Speech;
        p.stereo.widthPct = 140.0f; p.stereo.delayMs = 30.0f;
        p.clipThreshold = -3.0f;
        Buf l = noise ((int) FS * 6, 0.4f, 11), r = noise ((int) FS * 6, 0.4f, 12);
        // programme-like: sine plus noise
        const Buf s = sine (220, 0.5, (int) l.size());
        for (size_t i = 0; i < l.size(); ++i) { l[i] += s[i]; r[i] += 0.8f * s[i]; }

        // a few host block sizes in a row, as hosts do
        Result res { l, r, {}, {} };
        size_t pos = 0; const int sizes[] { 64, 480, 1000, 2048, 37 }; int si = 0;
        while (pos < l.size())
        {
            const int k = (int) std::min<size_t> ((size_t) sizes[si++ % 5], l.size() - pos);
            c.process (p, res.l.data() + pos, res.r.data() + pos, k, nullptr, nullptr, false, nullptr, nullptr);
            pos += (size_t) k;
        }
        bool finite = true; float mx = 0;
        for (size_t i = 0; i < l.size(); ++i) { finite &= std::isfinite (res.l[i]) && std::isfinite (res.r[i]); mx = std::max ({ mx, std::fabs (res.l[i]), std::fabs (res.r[i]) }); }
        CHECK (finite, "all stages on stays finite");
        const float ceil = dbToLin (-1.0f) * dbToLin (-3.0f);
        CHECK (mx <= ceil * 1.03f, "all on (bc=%d): sample peak %f within ceiling+trim %f", bc, mx, ceil);
        std::printf ("  all stages on, %s: latency %d samples, max sample %.3f, leveler gain %.1f dB\n",
                     bc ? "Broadcast" : "Live", c.latency(), mx, c.lev.gainDb());
    }
}

static void testModeSwitch()
{
    Chain c; c.prepare (FS);
    ChainParams p = plain(); p.on[sLim] = true; p.on[sEq] = true;
    Buf l = noise (48000, 0.3f, 21), r = l;
    for (int round = 0; round < 6; ++round)
    {
        p.broadcast = (round % 2) == 1;
        for (size_t i = 0; i < l.size(); i += 441)
            c.process (p, l.data() + i, r.data() + i, (int) std::min<size_t> (441, l.size() - i), nullptr, nullptr, false, nullptr, nullptr);
        for (float v : l) if (! std::isfinite (v)) { CHECK (false, "mode switch produced non-finite"); return; }
        CHECK (c.isBroadcast() == p.broadcast, "mode applied");
        l = noise (48000, 0.3f, 22 + (unsigned) round); r = l;
    }
}

int main()
{
    testLatency(); testBlockIndependence(); testBypass(); testSolo(); testEnableClick(); testAllOn(); testModeSwitch();
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
