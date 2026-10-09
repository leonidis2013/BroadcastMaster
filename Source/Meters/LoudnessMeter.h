#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include "Biquad.h"

namespace bm
{
// ITU-R BS.1770-4 loudness (momentary, short-term, gated integrated) plus EBU Tech 3342 loudness range.
// process() runs on the audio thread; the getters are safe to call from the UI thread.
class LoudnessMeter
{
public:
    static constexpr float kFloor = -120.0f;

    void prepare (double fs)
    {
        subBlockLen = std::max (1, (int) std::lround (fs * 0.1)); // 100 ms hop
        kw[0].prepare (fs);
        kw[1].prepare (fs);
        clearAll();
    }

    void requestReset() noexcept { resetRequested.store (true); }

    void process (const float* l, const float* r, int n) noexcept
    {
        if (resetRequested.exchange (false))
            clearMeasurements();

        for (int i = 0; i < n; ++i)
        {
            const double yl = kw[0].process (l[i]);
            const double yr = kw[1].process (r[i]);
            acc += yl * yl + yr * yr;

            if (++subCount >= subBlockLen)
            {
                finishSubBlock (acc / (double) subBlockLen);
                acc = 0.0;
                subCount = 0;
            }
        }
    }

    float momentary() const noexcept     { return mom.load (std::memory_order_relaxed); }
    float shortTerm() const noexcept     { return st.load (std::memory_order_relaxed); }
    float integrated() const noexcept    { return integ.load (std::memory_order_relaxed); }
    float range() const noexcept         { return lraVal.load (std::memory_order_relaxed); }
    float maxMomentary() const noexcept  { return maxMom.load (std::memory_order_relaxed); }
    float maxShortTerm() const noexcept  { return maxSt.load (std::memory_order_relaxed); }

private:
    static constexpr int kHistBins = 900;   // 0.1 LU bins, -70 .. +20 LUFS
    static constexpr int kRing = 30;        // 30 x 100 ms = 3 s

    struct Histogram
    {
        double count[kHistBins] {};
        double sumE[kHistBins] {};

        void clear() noexcept
        {
            std::fill (std::begin (count), std::end (count), 0.0);
            std::fill (std::begin (sumE), std::end (sumE), 0.0);
        }

        void add (double lufs, double energy) noexcept
        {
            int i = (int) std::floor ((lufs + 70.0) * 10.0);
            i = std::clamp (i, 0, kHistBins - 1);
            count[i] += 1.0;
            sumE[i] += energy;
        }
    };

    static double energyToLufs (double e) noexcept
    {
        return e > 1.0e-12 ? -0.691 + 10.0 * std::log10 (e) : (double) kFloor;
    }

    void finishSubBlock (double e) noexcept
    {
        ring[ringPos] = e;
        ringPos = (ringPos + 1) % kRing;
        filled = std::min (filled + 1, kRing);
        ++sinceReset;

        if (filled >= 4)
        {
            const double mE = meanOfLast (4);
            const double mL = energyToLufs (mE);
            mom.store ((float) mL, std::memory_order_relaxed);

            // Only blocks that lie entirely after the last reset count towards the measurements.
            if (sinceReset >= 4)
            {
                if ((float) mL > maxMom.load (std::memory_order_relaxed))
                    maxMom.store ((float) mL, std::memory_order_relaxed);

                if (mL >= -70.0)
                {
                    integHist.add (mL, mE);
                    integ.store ((float) computeIntegrated(), std::memory_order_relaxed);
                }
            }
        }

        if (filled >= kRing)
        {
            const double sE = meanOfLast (kRing);
            const double sL = energyToLufs (sE);
            st.store ((float) sL, std::memory_order_relaxed);

            if (sinceReset >= kRing && (float) sL > maxSt.load (std::memory_order_relaxed))
                maxSt.store ((float) sL, std::memory_order_relaxed);

            if (sinceReset >= kRing && sL >= -70.0)
            {
                lraHist.add (sL, sE);
                lraVal.store ((float) computeRange(), std::memory_order_relaxed);
            }
        }
    }

    double meanOfLast (int count) const noexcept
    {
        double s = 0.0;
        for (int k = 0; k < count; ++k)
            s += ring[(ringPos - 1 - k + kRing * 2) % kRing];
        return s / (double) count;
    }

    // Absolute gate -70 LUFS (applied when blocks are added), relative gate -10 LU.
    double computeIntegrated() const noexcept
    {
        double c = 0.0, s = 0.0;
        for (int i = 0; i < kHistBins; ++i) { c += integHist.count[i]; s += integHist.sumE[i]; }
        if (c <= 0.0) return kFloor;

        const double gamma = energyToLufs (s / c) - 10.0;
        const int start = std::max (0, (int) std::ceil ((gamma + 70.0 - 0.05) * 10.0));

        double c2 = 0.0, s2 = 0.0;
        for (int i = start; i < kHistBins; ++i) { c2 += integHist.count[i]; s2 += integHist.sumE[i]; }
        return c2 > 0.0 ? energyToLufs (s2 / c2) : (double) kFloor;
    }

    // EBU Tech 3342: absolute gate -70 LUFS, relative gate -20 LU, 10th to 95th percentile.
    double computeRange() const noexcept
    {
        double c = 0.0, s = 0.0;
        for (int i = 0; i < kHistBins; ++i) { c += lraHist.count[i]; s += lraHist.sumE[i]; }
        if (c <= 0.0) return 0.0;

        const double gamma = energyToLufs (s / c) - 20.0;
        const int start = std::max (0, (int) std::ceil ((gamma + 70.0 - 0.05) * 10.0));

        double total = 0.0;
        for (int i = start; i < kHistBins; ++i) total += lraHist.count[i];
        if (total <= 0.0) return 0.0;

        auto percentile = [&] (double p)
        {
            double run = 0.0;
            for (int i = start; i < kHistBins; ++i)
            {
                run += lraHist.count[i];
                if (run >= p * total)
                    return -70.0 + (double) i * 0.1 + 0.05;
            }
            return -70.0 + (double) (kHistBins - 1) * 0.1;
        };

        return std::max (0.0, percentile (0.95) - percentile (0.10));
    }

    void clearMeasurements() noexcept
    {
        sinceReset = 0;
        integHist.clear();
        lraHist.clear();
        integ.store (kFloor);
        lraVal.store (0.0f);
        maxMom.store (kFloor);
        maxSt.store (kFloor);
    }

    void clearAll() noexcept
    {
        kw[0].reset();
        kw[1].reset();
        std::fill (std::begin (ring), std::end (ring), 0.0);
        ringPos = 0;
        filled = 0;
        acc = 0.0;
        subCount = 0;
        mom.store (kFloor);
        st.store (kFloor);
        clearMeasurements();
    }

    KWeighting kw[2];
    int subBlockLen = 4800;
    int subCount = 0;
    double acc = 0.0;
    double ring[kRing] {};
    int ringPos = 0, filled = 0, sinceReset = 0;

    Histogram integHist, lraHist;

    std::atomic<bool> resetRequested { false };
    std::atomic<float> mom { kFloor }, st { kFloor }, integ { kFloor }, lraVal { 0.0f },
                       maxMom { kFloor }, maxSt { kFloor };
};
} // namespace bm
