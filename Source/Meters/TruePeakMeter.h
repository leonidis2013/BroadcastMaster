#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include "TpFilter.h"

namespace bm
{
// True-peak (inter-sample peak) meter in the spirit of BS.1770 Annex 2 (see TpFilter).
class TruePeakMeter
{
public:
    static constexpr float kFloor = -120.0f;

    void prepare (double fs)
    {
        filter.prepare (fs);
        subBlockLen = std::max (1, (int) std::lround (fs * 0.1));
        releaseCoeff = std::exp (-1.0 / (1.5 * fs));
        clearAll();
    }

    void requestReset() noexcept { resetRequested.store (true); }

    void process (const float* l, const float* r, int n) noexcept
    {
        if (resetRequested.exchange (false))
        {
            holdTp = 0.0f;
            holdSp = 0.0f;
        }

        for (int i = 0; i < n; ++i)
        {
            const float xs[2] { l[i], r[i] };
            float p = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
            {
                hist[ch][pos] = xs[ch];
                holdSp = std::max (holdSp, std::fabs (xs[ch]));
                p = std::max (p, filter.peak (hist[ch], pos));
            }
            pos = (pos + 1) & TpFilter::kHistMask;

            holdTp = std::max (holdTp, p);
            recent = std::max (p, (float) (recent * releaseCoeff));
            sbMax = std::max (sbMax, p);

            if (++sbCount >= subBlockLen)
            {
                ring[ringPos] = sbMax;
                ringPos = (ringPos + 1) % kRing;
                sbMax = 0.0f;
                sbCount = 0;

                float w = 0.0f;
                for (float v : ring) w = std::max (w, v);
                windowPeak.store (w, std::memory_order_relaxed);
            }
        }

        recentLin.store (recent, std::memory_order_relaxed);
        holdTpLin.store (holdTp, std::memory_order_relaxed);
        holdSpLin.store (holdSp, std::memory_order_relaxed);
    }

    // dBTP values; kFloor means "no signal".
    float recentDb() const noexcept   { return db (recentLin.load (std::memory_order_relaxed)); }
    float holdDb() const noexcept     { return db (holdTpLin.load (std::memory_order_relaxed)); }
    float samplePeakDb() const noexcept { return db (holdSpLin.load (std::memory_order_relaxed)); }
    float window3sDb() const noexcept { return db (windowPeak.load (std::memory_order_relaxed)); }

private:
    static constexpr int kRing = 30;

    static float db (float lin) noexcept
    {
        return lin > 1.0e-6f ? std::max (kFloor, 20.0f * std::log10 (lin)) : kFloor;
    }

    void clearAll() noexcept
    {
        for (auto& h : hist) std::fill (std::begin (h), std::end (h), 0.0f);
        std::fill (std::begin (ring), std::end (ring), 0.0f);
        pos = 0; ringPos = 0; sbCount = 0; sbMax = 0.0f;
        recent = 0.0f; holdTp = 0.0f; holdSp = 0.0f;
        recentLin.store (0.0f); holdTpLin.store (0.0f); holdSpLin.store (0.0f); windowPeak.store (0.0f);
    }

    TpFilter filter;
    int subBlockLen = 4800, sbCount = 0, ringPos = 0, pos = 0;
    double releaseCoeff = 0.99999;
    float sbMax = 0.0f, recent = 0.0f, holdTp = 0.0f, holdSp = 0.0f;
    float hist[2][TpFilter::kHistLen] {};
    float ring[kRing] {};

    std::atomic<bool> resetRequested { false };
    std::atomic<float> recentLin { 0.0f }, holdTpLin { 0.0f }, holdSpLin { 0.0f }, windowPeak { 0.0f };
};
} // namespace bm
