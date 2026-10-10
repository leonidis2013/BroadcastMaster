#pragma once
#include <atomic>
#include "DspCommon.h"

namespace bm::dsp
{
struct MbcBand
{
    bool on = true;
    float thrDb = -24.0f, ratio = 2.5f, attackMs = 20.0f, releaseMs = 200.0f, makeupDb = 0.0f;
};

struct MbcParams
{
    static constexpr int kBands = 4;
    float x[3] { 120.0f, 800.0f, 5000.0f };   // crossover frequencies
    MbcBand band[kBands];
    float kneeDb = 6.0f;
    float mixPct = 100.0f;                     // parallel (dry/wet) blend
};

// Four-band compressor on Linkwitz-Riley (LR4) crossovers. The bands are phase-aligned, so with no
// compression they sum back to an all-pass copy of the input (flat magnitude). Zero latency, stereo linked.
class MultibandComp
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        reset();
        setParams (params);
    }

    void reset() noexcept
    {
        for (auto& c : ch) c.reset();
        for (int b = 0; b < MbcParams::kBands; ++b) { envDb[b] = -120.0f; peak[b] = 0.0f; gr[b].store (0.0f); lvlOut[b].store (-120.0f); }
    }

    void setParams (const MbcParams& p)
    {
        params = p;
        double f1 = std::clamp ((double) p.x[0], 30.0, fs * 0.2);
        double f2 = std::clamp ((double) p.x[1], f1 * 1.3, fs * 0.3);
        double f3 = std::clamp ((double) p.x[2], f2 * 1.3, fs * 0.45);
        for (auto& c : ch) c.setFrequencies (fs, f1, f2, f3);

        for (int b = 0; b < MbcParams::kBands; ++b)
        {
            const auto& bd = p.band[b];
            atk[b] = (float) smoothCoef (std::max (0.1f, bd.attackMs) * 0.001, fs);
            rel[b] = (float) smoothCoef (std::max (1.0f, bd.releaseMs) * 0.001, fs);
            peakDecay[b] = (float) smoothCoef (b == 0 ? 0.03 : (b == 1 ? 0.015 : 0.008), fs);
        }
    }

    // soloBand: 0-3 copies that band's compressed output to soloL/soloR (which must hold n samples), -1 = none.
    void process (float* l, float* r, int n, int soloBand = -1, float* soloL = nullptr, float* soloR = nullptr) noexcept
    {
        float grMax[MbcParams::kBands] {};
        const float mix = std::clamp (params.mixPct, 0.0f, 100.0f) * 0.01f;

        for (int i = 0; i < n; ++i)
        {
            double bl[MbcParams::kBands], br[MbcParams::kBands];
            ch[0].split (l[i], bl);
            ch[1].split (r[i], br);

            float outL = 0.0f, outR = 0.0f, dryL = 0.0f, dryR = 0.0f;
            for (int b = 0; b < MbcParams::kBands; ++b)
            {
                const float a = (float) bl[b], c = (float) br[b];
                dryL += a; dryR += c;

                float g = 1.0f;
                const auto& bd = params.band[b];
                if (bd.on)
                {
                    const float lvl = std::max (std::fabs (a), std::fabs (c));
                    peak[b] = std::max (lvl, peak[b] * peakDecay[b]);
                    const float lvlDb = linToDb (peak[b]);
                    const float k = lvlDb > envDb[b] ? atk[b] : rel[b];
                    envDb[b] = k * envDb[b] + (1.0f - k) * lvlDb;

                    const float grDb = gainReductionDb (envDb[b], bd.thrDb, bd.ratio, params.kneeDb);
                    grMax[b] = std::max (grMax[b], grDb);
                    g = dbToLin (-grDb + bd.makeupDb);
                }

                const float ya = a * g, yc = c * g;
                outL += ya; outR += yc;
                if (b == soloBand && soloL && soloR) { soloL[i] = ya; soloR[i] = yc; }
            }

            l[i] = dryL + (outL - dryL) * mix;
            r[i] = dryR + (outR - dryR) * mix;
        }

        for (int b = 0; b < MbcParams::kBands; ++b)
        {
            gr[b].store (grMax[b], std::memory_order_relaxed);
            lvlOut[b].store (envDb[b], std::memory_order_relaxed);
        }
    }

    // Largest gain reduction (dB, positive) seen in the last block, per band.
    float grDb (int band) const noexcept { return gr[band].load (std::memory_order_relaxed); }

    // Detector level (dB) of a band after the last block, the value that is compared with the threshold.
    float levelDb (int band) const noexcept { return lvlOut[band].load (std::memory_order_relaxed); }

    // Static compression curve: reduction in dB for a given level.
    static float gainReductionDb (float levelDb, float thrDb, float ratio, float kneeDb) noexcept
    {
        const float over = levelDb - thrDb;
        const float slope = 1.0f - 1.0f / std::max (1.0f, ratio);
        if (kneeDb > 0.01f && std::fabs (over) <= 0.5f * kneeDb)
        {
            const float t = over + 0.5f * kneeDb;
            return slope * t * t / (2.0f * kneeDb);
        }
        return over > 0.0f ? slope * over : 0.0f;
    }

private:
    struct Lr4
    {
        bm::Biquad a, b;
        double process (double x) noexcept { return b.process (a.process (x)); }
        void reset() noexcept { a.reset(); b.reset(); }
    };

    struct Crossover
    {
        Lr4 lp, hp;
        void set (double fs, double f)
        {
            const Coef l = lowpass (fs, f, 0.7071067811865476), h = highpass (fs, f, 0.7071067811865476);
            l.applyTo (lp.a); l.applyTo (lp.b); h.applyTo (hp.a); h.applyTo (hp.b);
        }
        void reset() noexcept { lp.reset(); hp.reset(); }
        double allpass (double x) noexcept { return lp.process (x) + hp.process (x); }
    };

    struct Channel
    {
        Crossover x1, x2, x3;          // the three splits
        Crossover ap2For1, ap3For1;    // phase compensation for band 1
        Crossover ap3For2;             // phase compensation for band 2

        void setFrequencies (double fs, double f1, double f2, double f3)
        {
            x1.set (fs, f1); x2.set (fs, f2); x3.set (fs, f3);
            ap2For1.set (fs, f2); ap3For1.set (fs, f3); ap3For2.set (fs, f3);
        }

        void reset() noexcept { x1.reset(); x2.reset(); x3.reset(); ap2For1.reset(); ap3For1.reset(); ap3For2.reset(); }

        void split (double x, double* out) noexcept
        {
            const double lo1 = x1.lp.process (x), hi1 = x1.hp.process (x);
            const double lo2 = x2.lp.process (hi1), hi2 = x2.hp.process (hi1);
            const double lo3 = x3.lp.process (hi2), hi3 = x3.hp.process (hi2);
            out[0] = ap3For1.allpass (ap2For1.allpass (lo1));
            out[1] = ap3For2.allpass (lo2);
            out[2] = lo3;
            out[3] = hi3;
        }
    };

    double fs = 48000.0;
    MbcParams params;
    Channel ch[2];
    float envDb[MbcParams::kBands] {}, peak[MbcParams::kBands] {};
    float atk[MbcParams::kBands] {}, rel[MbcParams::kBands] {}, peakDecay[MbcParams::kBands] {};
    std::atomic<float> gr[MbcParams::kBands] {}, lvlOut[MbcParams::kBands] {};
};
} // namespace bm::dsp
