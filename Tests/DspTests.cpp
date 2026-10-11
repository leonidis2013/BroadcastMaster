// Known-signal tests for the BroadcastMaster DSP modules (no JUCE needed).
// Build: g++ -std=c++17 -O2 -Wall -Wextra -ISource Tests/DspTests.cpp -o dsp_tests && ./dsp_tests
#include <cstdio>
#include <functional>
#include <memory>
#include <random>
#include <vector>
#include "Dsp/AutoLeveler.h"
#include "Dsp/MultibandComp.h"
#include "Dsp/SimpleModules.h"
#include "Dsp/TonalEq.h"
#include "Dsp/Tonality.h"
#include "Dsp/TruePeakLimiter.h"

using namespace bm::dsp;

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL line %d: ", __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)
static void near (double v, double want, double tol, const char* what)
{
    ++checks;
    if (std::fabs (v - want) > tol) { ++failures; std::printf ("FAIL %s: got %.4f want %.4f +-%.4f\n", what, v, want, tol); }
}

constexpr double FS = 48000.0;
using Buf = std::vector<float>;

static Buf sine (double f, double amp, int n, double fs = FS)
{
    Buf b ((size_t) n);
    for (int i = 0; i < n; ++i) b[(size_t) i] = (float) (amp * std::sin (2.0 * bm::kPi * f * i / fs));
    return b;
}

static double rmsDb (const Buf& b, int from)
{
    double s = 0; int c = 0;
    for (size_t i = (size_t) from; i < b.size(); ++i) { s += (double) b[i] * b[i]; ++c; }
    return 10.0 * std::log10 (std::max (1e-20, s / std::max (1, c)));
}

// Runs stereo (same signal both channels) through fn in chunks.
static void runChunks (Buf& l, Buf& r, int chunk, const std::function<void (float*, float*, int)>& fn)
{
    for (size_t i = 0; i < l.size(); i += (size_t) chunk)
        fn (l.data() + i, r.data() + i, (int) std::min<size_t> ((size_t) chunk, l.size() - i));
}

static double gainDbAt (double f, const std::function<void (float*, float*, int)>& fn, int skip = 24000, double amp = 0.1)
{
    Buf l = sine (f, amp, 48000 + skip), r = l;
    const Buf in = l;
    runChunks (l, r, 480, fn);
    Buf a (l.begin() + skip, l.end()), b (in.begin() + skip, in.end());
    return rmsDb (a, 0) - rmsDb (b, 0);
}

// 8x oversampled true-peak checker, independent of the limiter's own filter (long Kaiser sinc).
static double truePeakDb (const Buf& x, int from)
{
    const int up = 8, taps = 32;
    auto i0 = [] (double v) { double s = 1, t = 1; for (int k = 1; k < 50; ++k) { t *= (v / (2.0 * k)) * (v / (2.0 * k)); s += t; } return s; };
    std::vector<double> h ((size_t) (2 * taps * up + 1));
    for (size_t n = 0; n < h.size(); ++n)
    {
        const double t = ((double) n - taps * up) / up, r = t / taps;
        const double sinc = std::fabs (t) < 1e-12 ? 1.0 : std::sin (bm::kPi * t) / (bm::kPi * t);
        h[n] = sinc * i0 (9.0 * std::sqrt (std::max (0.0, 1 - r * r))) / i0 (9.0);
    }
    double peak = 0;
    for (int n = from; n < (int) x.size() - taps - 1; ++n)
        for (int p = 0; p < up; ++p)
        {
            double y = 0;
            for (int k = -taps + 1; k <= taps; ++k)
            {
                const int idx = n + k;
                if (idx < 0 || idx >= (int) x.size()) continue;
                const int hi = (int) (taps * up) + p - k * up;
                if (hi >= 0 && hi < (int) h.size()) y += x[(size_t) idx] * h[(size_t) hi];
            }
            peak = std::max (peak, std::fabs (y));
        }
    return 20.0 * std::log10 (std::max (1e-12, peak));
}

static void testHpf()
{
    SubsonicHpf h; h.prepare (FS); h.setFrequency (30.0);
    auto fn = [&] (float* l, float* r, int n) { h.process (l, r, n); };
    near (gainDbAt (1000, fn), 0.0, 0.05, "hpf passband");
    h.reset();
    near (gainDbAt (30, fn, 48000), -3.0, 0.4, "hpf corner -3 dB");
    h.reset();
    CHECK (gainDbAt (10, fn, 48000) < -20.0, "hpf 10 Hz attenuated");
}

static void testPhaseRotator()
{
    PhaseRotator p; p.prepare (FS); p.setParams (160, 3);
    auto fn = [&] (float* l, float* r, int n) { p.process (l, r, n); };
    for (double f : { 100.0, 400.0, 2000.0, 8000.0 })
    {
        p.reset();
        near (gainDbAt (f, fn), 0.0, 0.05, "phase rotator magnitude flat");
    }
}

static void testEq()
{
    EqParams ep; ep.band[2].gainDb = 6.0f; ep.band[2].freq = 1000.0f; ep.band[2].q = 1.0f;

    // Live mode (IIR)
    {
        TonalEq eq; eq.prepare (FS); eq.setLinearPhase (false); eq.setParams (ep);
        auto fn = [&] (float* l, float* r, int n) { eq.process (l, r, n); };
        near (gainDbAt (1000, fn), 6.0, 0.15, "eq live +6 at 1k");
        eq.reset();
        near (gainDbAt (60, fn, 48000), 0.0, 0.8, "eq live far away");
        CHECK (eq.latency() == 0, "live latency 0");
    }

    // Broadcast mode (linear phase)
    {
        TonalEq eq; eq.prepare (FS); eq.setLinearPhase (true); eq.setParams (ep);
        KernelPair k; eq.design (k); eq.applyKernelsNow (k);
        CHECK (eq.latency() == kEqLatency, "linear latency");
        auto fn = [&] (float* l, float* r, int n) { eq.process (l, r, n); };
        near (gainDbAt (1000, fn, 36000), 6.0, 0.4, "eq linear +6 at 1k");
        eq.reset();
        near (gainDbAt (200, fn, 36000), TonalEq::responseDb (ep, 0, 200, FS), 0.5, "eq linear matches IIR magnitude at 200");
    }

    // Flat kernel = pure delay by kEqLatency; and chunk-size independence
    {
        auto run = [&] (int chunk)
        {
            TonalEq eq; eq.prepare (FS); eq.setLinearPhase (true);
            Buf l (8000, 0.0f), r (8000, 0.0f); l[100] = 1.0f; r[100] = -0.5f;
            runChunks (l, r, chunk, [&] (float* a, float* b, int n) { eq.process (a, b, n); });
            return std::make_pair (l, r);
        };
        auto a = run (64), b = run (1000);
        const int pos = 100 + kEqLatency;
        near (a.first[(size_t) pos], 1.0, 1e-3, "flat kernel delayed impulse L");
        near (a.second[(size_t) pos], -0.5, 1e-3, "flat kernel delayed impulse R");
        double d = 0; for (size_t i = 0; i < a.first.size(); ++i) d = std::max (d, (double) std::fabs (a.first[i] - b.first[i]));
        CHECK (d < 1e-6, "chunk independence %g", d);
        double off = 0; for (size_t i = 0; i < a.first.size(); ++i) if ((int) i != pos) off = std::max (off, (double) std::fabs (a.first[i]));
        CHECK (off < 1e-3, "flat kernel no other taps %g", off);
    }

    // Mid-only vs side-only targets
    {
        EqParams sp; sp.band[2].gainDb = 6.0f; sp.band[2].target = 2; // side only
        TonalEq eq; eq.prepare (FS); eq.setLinearPhase (false); eq.setParams (sp);
        Buf l = sine (1000, 0.1, 48000), r = l; // mono => no side => unchanged
        const Buf in = l;
        runChunks (l, r, 480, [&] (float* a, float* b, int n) { eq.process (a, b, n); });
        near (rmsDb (l, 24000) - rmsDb (in, 24000), 0.0, 0.05, "side-only EQ leaves mono untouched");
    }
}

static void testSaturation()
{
    for (int t = 0; t < 3; ++t)
    {
        Saturator s; s.prepare (FS); s.setParams ((SatType) t, 50.0f, 100.0f);
        auto fn = [&] (float* l, float* r, int n) { s.process (l, r, n); };
        near (gainDbAt (500, fn, 24000, 0.001), 0.0, 0.3, "saturation small signal near unity");
    }
    // harmonics: tube at high drive adds 2nd harmonic, tape adds 3rd
    for (int t = 0; t < 2; ++t)
    {
        Saturator s; s.prepare (FS); s.setParams ((SatType) t, 100.0f, 100.0f);
        Buf l = sine (1000, 0.5, 48000), r = l;
        runChunks (l, r, 480, [&] (float* a, float* b, int n) { s.process (a, b, n); });
        double a2 = 0, b2 = 0, a3 = 0, b3 = 0; // single-bin DFT at 2k and 3k
        for (int i = 24000; i < 48000; ++i)
        {
            const double x = l[(size_t) i], w2 = 2 * bm::kPi * 2000 * i / FS, w3 = 2 * bm::kPi * 3000 * i / FS;
            a2 += x * std::cos (w2); b2 += x * std::sin (w2); a3 += x * std::cos (w3); b3 += x * std::sin (w3);
        }
        const double h2 = std::hypot (a2, b2), h3 = std::hypot (a3, b3);
        if (t == 0) CHECK (h2 > 100.0, "tube has 2nd harmonic %g", h2);
        else CHECK (h3 > 100.0, "tape has 3rd harmonic %g", h3);
    }
    // zero amount = bypass
    {
        Saturator s; s.prepare (FS); s.setParams (SatType::Tube, 0.0f, 100.0f);
        Buf l = sine (1000, 0.5, 4800), r = l; const Buf in = l;
        s.process (l.data(), r.data(), 4800);
        CHECK (l == in, "saturator amount 0 is exact bypass");
    }
}

static void testEnhancer()
{
    ToneEnhancer e; e.prepare (FS);
    e.setParams (4500, 0, 80, 0);
    { Buf l = sine (200, 0.3, 4800), r = l; const Buf in = l; e.process (l.data(), r.data(), 4800); CHECK (l == in, "enhancer 0 is bypass"); }
    // exciter: 8 kHz in gets extra energy at 16k? use 6 kHz tone -> 3rd harmonic 18k; compare high energy increase
    e.reset(); e.setParams (4000, 100, 80, 0);
    Buf l = sine (6000, 0.3, 48000), r = l; const Buf in = l;
    runChunks (l, r, 480, [&] (float* a, float* b, int n) { e.process (a, b, n); });
    double diff = 0; for (size_t i = 24000; i < l.size(); ++i) diff += std::fabs (l[i] - in[i]);
    CHECK (diff / 24000.0 > 0.002, "exciter adds content %g", diff / 24000.0);
    // bass: 60 Hz tone produces harmonics (output differs from input)
    e.reset(); e.setParams (4000, 0, 80, 100);
    Buf bl = sine (60, 0.3, 48000), br = bl; const Buf bin = bl;
    runChunks (bl, br, 480, [&] (float* a, float* b, int n) { e.process (a, b, n); });
    double bd = 0; for (size_t i = 24000; i < bl.size(); ++i) bd += std::fabs (bl[i] - bin[i]);
    CHECK (bd / 24000.0 > 0.002, "bass enhancer adds content %g", bd / 24000.0);
    for (float v : bl) CHECK (std::isfinite (v), "bass finite");
}

static void testMbc()
{
    MbcParams p;
    for (auto& b : p.band) b.on = false;
    MultibandComp m; m.prepare (FS); m.setParams (p);
    auto fn = [&] (float* l, float* r, int n) { m.process (l, r, n); };
    for (double f : { 60.0, 120.0, 300.0, 800.0, 2000.0, 5000.0, 12000.0 })
    {
        m.reset();
        near (gainDbAt (f, fn), 0.0, 0.15, "mbc crossover sum flat");
    }

    // static curve at 2 kHz (band 3), amplitude 0.25 => peak -12 dBFS
    MbcParams q;
    for (auto& b : q.band) { b.on = true; b.thrDb = -24.0f; b.ratio = 4.0f; b.attackMs = 5; b.releaseMs = 100; }
    q.kneeDb = 0.0f;
    m.setParams (q); m.reset();
    const double g = gainDbAt (2000, fn, 24000, 0.25);
    const double want = -MultibandComp::gainReductionDb (-12.0f, -24.0f, 4.0f, 0.0f); // -9 dB
    near (g, want, 1.5, "mbc static compression");
    CHECK (m.grDb (2) > 5.0, "gr meter reads %g", m.grDb (2));
    near (MultibandComp::gainReductionDb (-24.0f, -24.0f, 4.0f, 6.0f), 0.5 * 0.75 * 3.0 * 3.0 / (2.0 * 6.0) * 1.0, 0.5, "knee at threshold small");

    // solo output equals band signal content
    MbcParams z; for (auto& b : z.band) b.on = false;
    m.setParams (z); m.reset();
    Buf l = sine (60, 0.2, 24000), r = l, sl (24000, 0.0f), sr (24000, 0.0f);
    m.process (l.data(), r.data(), 24000, 0, sl.data(), sr.data());
    CHECK (rmsDb (sl, 12000) > -20.0, "band 1 solo carries 60 Hz");
    Buf l2 = sine (60, 0.2, 24000), r2 = l2, s2l (24000, 0.0f), s2r (24000, 0.0f);
    m.reset();
    m.process (l2.data(), r2.data(), 24000, 3, s2l.data(), s2r.data());
    CHECK (rmsDb (s2l, 12000) < -60.0, "band 4 solo does not carry 60 Hz");
}

static Buf shapedNoise (int n, bool speechLike, unsigned seed, double levelDb)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> nd (0.0f, 1.0f);
    Buf b ((size_t) n);
    const float amp = (float) std::pow (10.0, levelDb / 20.0);
    for (int i = 0; i < n; ++i)
    {
        float env = 1.0f;
        if (speechLike)
        {
            const double t = i / FS;
            const double syll = std::fmod (t, 0.25) / 0.25;   // 4 syllables per second
            env = syll < 0.55 ? 1.0f : 0.02f;                  // gaps between syllables
            const double word = std::fmod (t, 1.5);           // longer pauses
            if (word > 1.2) env *= 0.05f;
        }
        b[(size_t) i] = amp * env * nd (rng);
    }
    return b;
}

static void testLeveler()
{
    // convergence: steady 1 kHz sine well below target in Speech mode
    {
        AutoLeveler lv; lv.prepare (FS);
        LevParams p; p.targetLufs = -20.0f; p.rangeDb = 12.0f; p.mode = LevMode::Speech; p.speed = LevSpeed::Medium;
        lv.setParams (p);
        Buf l = sine (1000, 0.0316, (int) (FS * 25)), r = l; // about -30 dBFS peak = -33 LUFS-ish
        runChunks (l, r, 480, [&] (float* a, float* b, int n) { lv.process (a, b, n); });
        const double outLufs = lv.levelLufs() + lv.gainDb();
        near (outLufs, -20.0, 1.0, "leveler converges to target");
        CHECK (lv.gainDb() > 6.0f, "leveler gain positive %g", lv.gainDb());

        // gate: silence leaves the gain frozen
        const float g0 = lv.gainDb();
        Buf sl (480000, 0.0f), sr (480000, 0.0f);
        runChunks (sl, sr, 480, [&] (float* a, float* b, int n) { lv.process (a, b, n); });
        near (lv.gainDb(), g0, 0.8, "gate freezes gain in silence");
    }
    // range limits
    {
        AutoLeveler lv; lv.prepare (FS);
        LevParams p; p.targetLufs = -10.0f; p.rangeDb = 6.0f; p.mode = LevMode::Speech; p.speed = LevSpeed::Fast;
        lv.setParams (p);
        Buf l = sine (1000, 0.01, (int) (FS * 20)), r = l;
        runChunks (l, r, 480, [&] (float* a, float* b, int n) { lv.process (a, b, n); });
        CHECK (lv.gainDb() <= 6.01f, "range limit holds %g", lv.gainDb());
    }
    // pauses: a talker at a steady level with 1.5 s breaks. The gain must hold through the pause and not creep up.
    {
        AutoLeveler lv; lv.prepare (FS);
        LevParams p; p.targetLufs = -20.0f; p.rangeDb = 12.0f; p.mode = LevMode::Auto; p.speed = LevSpeed::Medium;
        lv.setParams (p);
        auto talk = [&] (double sec, double dbfs, unsigned seed)
        {
            Buf l = shapedNoise ((int) (FS * sec), true, seed, dbfs), r = l;
            runChunks (l, r, 480, [&] (float* a, float* b, int n) { lv.process (a, b, n); });
        };
        auto pause = [&] (double sec)
        {
            Buf l ((size_t) (FS * sec), 0.0f), r = l;
            runChunks (l, r, 480, [&] (float* a, float* b, int n) { lv.process (a, b, n); });
        };
        talk (20, -34, 5);
        const float gTalk = lv.gainDb();
        pause (4.0);
        near (lv.gainDb(), gTalk, 0.3, "gain holds through a 4 s pause");
        talk (6, -34, 6);
        near (lv.gainDb(), gTalk, 1.5, "gain after the pause is where it was");
    }
    // talk to a much louder service: the output must reach the target region within a few seconds
    {
        AutoLeveler lv; lv.prepare (FS);
        LevParams p; p.targetLufs = -20.0f; p.rangeDb = 12.0f; p.mode = LevMode::Auto; p.speed = LevSpeed::Medium;
        lv.setParams (p);
        Buf a = shapedNoise ((int) (FS * 15), true, 7, -38), ar = a;
        runChunks (a, ar, 480, [&] (float* x, float* y, int n) { lv.process (x, y, n); });
        const float gTalk = lv.gainDb();
        CHECK (gTalk > 3.0f, "talk is lifted %g", gTalk);
        Buf m = shapedNoise ((int) (FS * 6), false, 8, -22), mr = m; // music-like: continuous, 16 dB louder
        runChunks (m, mr, 480, [&] (float* x, float* y, int n) { lv.process (x, y, n); });
        CHECK (lv.gainDb() < gTalk - 6.0f, "gain comes down for loud music %g -> %g", gTalk, lv.gainDb());
        near (lv.levelLufs() + lv.gainDb(), -20.0, 3.0, "loud music lands near target within 6 s");
    }
    // speech vs music classification
    {
        AutoLeveler a, b; a.prepare (FS); b.prepare (FS);
        LevParams p; p.targetLufs = -20.0f; p.mode = LevMode::Auto;
        a.setParams (p); b.setParams (p);
        Buf sp = shapedNoise ((int) (FS * 30), true, 1, -25), spr = sp;
        Buf mu = shapedNoise ((int) (FS * 30), false, 2, -25), mur = mu;
        runChunks (sp, spr, 480, [&] (float* x, float* y, int n) { a.process (x, y, n); });
        runChunks (mu, mur, 480, [&] (float* x, float* y, int n) { b.process (x, y, n); });
        CHECK (a.speechAmount() > 0.7f, "speech detected %g", a.speechAmount());
        CHECK (b.speechAmount() < 0.3f, "music detected %g", b.speechAmount());
    }
}

static void testStereoUtils()
{
    StereoUtils::Params p;
    auto make = [&] (const StereoUtils::Params& pp) { auto* s = new StereoUtils; s->prepare (FS); s->setParams (pp); return std::unique_ptr<StereoUtils> (s); };

    p.widthPct = 0.0f;
    { auto s = make (p); Buf l = sine (500, 0.3, 4800), r = sine (700, 0.3, 4800);
      s->process (l.data(), r.data(), 4800);
      double d = 0; for (int i = 0; i < 4800; ++i) d = std::max (d, (double) std::fabs (l[(size_t) i] - r[(size_t) i]));
      CHECK (d < 1e-6, "width 0 gives mono %g", d); }

    p = {}; p.mono = true;
    { auto s = make (p); Buf l (100, 1.0f), r (100, 0.0f); s->process (l.data(), r.data(), 100);
      near (l[50], 0.5, 1e-6, "mono sum L"); near (r[50], 0.5, 1e-6, "mono sum R"); }

    p = {}; p.flipL = true;
    { auto s = make (p); Buf l (100, 0.5f), r (100, 0.5f); s->process (l.data(), r.data(), 100);
      near (l[10], -0.5, 1e-6, "flip L"); near (r[10], 0.5, 1e-6, "R untouched"); }

    p = {}; p.widthPct = 200.0f;
    { auto s = make (p); Buf l (100, 1.0f), r (100, 0.0f); s->process (l.data(), r.data(), 100);
      near (l[10], 1.5, 1e-6, "width 200 L"); near (r[10], -0.5, 1e-6, "width 200 R"); }

    p = {}; p.midGainDb = 6.0206f;
    { auto s = make (p); Buf l (100, 0.25f), r (100, 0.25f); s->process (l.data(), r.data(), 100);
      near (l[10], 0.5, 1e-3, "mid gain +6 dB"); }

    // stereo exciter: adds harmonics to the side signal only, never touches the mono sum, off = transparent
    p = {}; p.excitePct = 0.0f;
    { auto s = make (p); Buf l = sine (1000, 0.3, 9600), r = sine (1300, 0.2, 9600), l0 = l, r0 = r;
      s->process (l.data(), r.data(), 9600);
      double d = 0; for (int i = 0; i < 9600; ++i) d = std::max (d, (double) std::fabs (l[(size_t) i] - l0[(size_t) i]));
      CHECK (d < 1e-7, "exciter at 0 is transparent %g", d); }
    p = {}; p.excitePct = 100.0f; p.exciteHz = 3000.0f;
    { auto s = make (p); Buf l = sine (5000, 0.15, 9600), r = l;
      for (auto& v : r) v = -v;   // pure side signal
      Buf l0 = l, r0 = r;
      s->process (l.data(), r.data(), 9600);
      double sumDiff = 0, sideDiff = 0;
      for (int i = 4800; i < 9600; ++i)
      {
          sumDiff = std::max (sumDiff, (double) std::fabs ((l[(size_t) i] + r[(size_t) i]) - (l0[(size_t) i] + r0[(size_t) i])));
          sideDiff = std::max (sideDiff, (double) std::fabs ((l[(size_t) i] - r[(size_t) i]) - (l0[(size_t) i] - r0[(size_t) i])));
      }
      CHECK (sumDiff < 1e-5, "exciter leaves the mono sum alone %g", sumDiff);
      CHECK (sideDiff > 1e-3, "exciter changes the side signal %g", sideDiff); }

    p = {}; p.delayMs = 10.0f;
    { auto s = make (p); Buf l (4800, 0.0f), r (4800, 0.0f); l[0] = 1.0f;
      // the delay change fades in over 20 ms, so warm up with the setting before measuring
      Buf wl (4800, 0.0f), wr (4800, 0.0f); s->process (wl.data(), wr.data(), 4800);
      s->process (l.data(), r.data(), 4800);
      near (l[(size_t) std::lround (0.01 * FS)], 1.0, 1e-6, "delay 10 ms"); }
}

static void testSoftClipper()
{
    SoftClipper c; c.prepare (FS); c.setParams (-3.0f, 50.0f);
    const float th = dbToLin (-3.0f);
    Buf l = sine (300, 4.0, 48000), r = l;
    c.process (l.data(), r.data(), 48000);
    float mx = 0; for (float v : l) mx = std::max (mx, std::fabs (v));
    CHECK (mx <= th * 1.0005f, "clipper bounded %g > %g", mx, th);
    c.reset();
    Buf s = sine (300, 0.1, 4800), s2 = s; const Buf in = s;
    c.process (s.data(), s2.data(), 4800);
    // first-order ADAA averages adjacent samples (a half-sample delay), so compare with that
    double d = 0; for (size_t i = 5; i < s.size(); ++i) d = std::max (d, (double) std::fabs (s[i] - 0.5f * (in[i] + in[i - 1])));
    CHECK (d < 1e-5, "clipper linear region %g", d);
    CHECK (std::fabs (c.shape (th * 0.4f) - th * 0.4f) < 1e-6f, "shape linear below knee");
}

static void testLimiter()
{
    // true peak stays under the ceiling on hostile material
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        TruePeakLimiter lim; lim.prepare (fs);
        lim.setParams (-1.0f, 100.0f, 3.0f, true);
        std::mt19937 rng (7);
        std::normal_distribution<float> nd (0.0f, 1.0f);
        const int n = (int) (fs * 4);
        Buf l ((size_t) n), r ((size_t) n);
        // programme-like hostile material: noise limited to 16 kHz (4th-order low-pass) plus an 11 kHz tone, with loud bursts
        bm::Biquad lp[2][2];
        const Coef lpc = lowpass (fs, 16000.0, 0.7071);
        for (auto& a : lp) for (auto& b : a) lpc.applyTo (b);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / fs;
            float burst = (std::fmod (t, 0.5) < 0.05) ? 2.5f : 0.4f;
            const double nl = lp[0][1].process (lp[0][0].process (nd (rng))), nr = lp[1][1].process (lp[1][0].process (nd (rng)));
            l[(size_t) i] = burst * (0.6f * (float) nl + 0.5f * (float) std::sin (2 * bm::kPi * 11025.0 * t + 0.7));
            r[(size_t) i] = burst * (0.6f * (float) nr + 0.5f * (float) std::sin (2 * bm::kPi * 11025.0 * t));
        }
        for (auto& v : l) v = std::clamp (v, -4.0f, 4.0f);
        runChunks (l, r, 512, [&] (float* a, float* b, int k) { lim.process (a, b, k); });
        const double tpL = truePeakDb (l, lim.latency() + 100), tpR = truePeakDb (r, lim.latency() + 100);
        char msg[96]; std::snprintf (msg, sizeof msg, "limiter true peak fs=%.0f", fs);
        CHECK (std::max (tpL, tpR) <= -1.0 + 0.02, "%s: %.3f dBTP", msg, std::max (tpL, tpR));
    }

    // inter-sample peaks: fs/4 sine at 45 degrees has sample peak 0.707 of true peak
    {
        TruePeakLimiter lim; lim.prepare (FS); lim.setParams (-1.0f, 100.0f, 3.0f, true);
        Buf l ((size_t) FS), r;
        for (int i = 0; i < (int) FS; ++i) l[(size_t) i] = (float) (1.4 * std::sin (2 * bm::kPi * (FS / 4.0) * i / FS + bm::kPi / 4.0));
        r = l;
        runChunks (l, r, 256, [&] (float* a, float* b, int k) { lim.process (a, b, k); });
        const double tp = truePeakDb (l, lim.latency() + 200);
        CHECK (tp <= -1.0 + 0.02, "intersample limited %.3f", tp);
        CHECK (tp > -1.6, "not over-limited %.3f", tp);
    }

    // below the ceiling: pure delay, unity gain
    {
        TruePeakLimiter lim; lim.prepare (FS); lim.setParams (-1.0f, 100.0f, 3.0f, true);
        Buf l = sine (1000, 0.3, 9600), r = l; const Buf in = l;
        runChunks (l, r, 480, [&] (float* a, float* b, int k) { lim.process (a, b, k); });
        const int D = lim.latency();
        double d = 0; for (size_t i = (size_t) D + 10; i < l.size(); ++i) d = std::max (d, (double) std::fabs (l[i] - in[i - (size_t) D]));
        CHECK (d < 1e-6, "limiter transparent below ceiling %g", d);
        CHECK (lim.grDb() < 0.001f, "no gr %g", lim.grDb());
    }

    // delta output = delayed input - output
    {
        TruePeakLimiter lim; lim.prepare (FS); lim.setParams (-6.0f, 50.0f, 2.0f, true);
        Buf l = sine (1000, 0.9, 9600), r = l, dl (9600), dr (9600); const Buf in = l;
        lim.process (l.data(), r.data(), 9600, dl.data(), dr.data());
        const int D = lim.latency();
        double d = 0; for (size_t i = (size_t) D; i < l.size(); ++i) d = std::max (d, (double) std::fabs (dl[i] - (in[i - (size_t) D] - l[i])));
        CHECK (d < 1e-6, "delta consistent %g", d);
    }

    // disabled: pure delay of the same length (latency does not change)
    {
        TruePeakLimiter lim; lim.prepare (FS); lim.setParams (-1.0f, 100.0f, 3.0f, false);
        Buf l = sine (1000, 1.5, 9600), r = l; const Buf in = l;
        const int D = lim.latency();
        runChunks (l, r, 480, [&] (float* a, float* b, int k) { lim.process (a, b, k); });
        double d = 0; for (size_t i = (size_t) D; i < l.size(); ++i) d = std::max (d, (double) std::fabs (l[i] - in[i - (size_t) D]));
        CHECK (d < 1e-6, "disabled limiter is a pure delay %g", d);
    }
}

int main()
{
    testHpf(); testPhaseRotator(); testEq(); testSaturation(); testEnhancer(); testMbc();
    testLeveler(); testStereoUtils(); testSoftClipper(); testLimiter();
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
