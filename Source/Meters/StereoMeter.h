// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include "Biquad.h"

namespace bm
{
// Stereo image metering: correlation, L/R balance, mid/side levels, mono-compatibility loss and a ring
// buffer of recent L/R samples for the goniometer.
class StereoMeter
{
public:
    static constexpr int kScopeSize = 4096;
    static constexpr float kFloor = -120.0f;

    StereoMeter()
    {
        for (auto& s : scopeL) s.store (0.0f);
        for (auto& s : scopeR) s.store (0.0f);
    }

    void prepare (double fs)
    {
        sampleRate = fs;
        smooth = std::exp (-1.0 / (0.3 * fs));          // 300 ms
        peakRelease = std::exp (-1.0 / (1.0 * fs));     // 1 s peak fall-back
        for (auto& s : scopeL) s.store (0.0f);
        for (auto& s : scopeR) s.store (0.0f);
        scopeWrite.store (0);
        sxx = syy = sxy = smm = sss = smono = 0.0;
        midPk = sidePk = 0.0;
        corr.store (0.0f); balance.store (0.0f);
        midRms.store (kFloor); sideRms.store (kFloor);
        midPeak.store (kFloor); sidePeak.store (kFloor);
        monoLoss.store (0.0f);
    }

    void process (const float* l, const float* r, int n) noexcept
    {
        uint32_t w = scopeWrite.load (std::memory_order_relaxed);

        for (int i = 0; i < n; ++i)
        {
            const double a = l[i], b = r[i];
            const double m = (a + b) * 0.5, s = (a - b) * 0.5, mono = m;   // mid/side as half-sum and half-difference: a mono signal reads its channel level

            sxx   = smooth * sxx   + (1.0 - smooth) * a * a;
            syy   = smooth * syy   + (1.0 - smooth) * b * b;
            sxy   = smooth * sxy   + (1.0 - smooth) * a * b;
            smm   = smooth * smm   + (1.0 - smooth) * m * m;
            sss   = smooth * sss   + (1.0 - smooth) * s * s;
            smono = smooth * smono + (1.0 - smooth) * mono * mono;

            midPk  = std::max (std::fabs (m), midPk * peakRelease);
            sidePk = std::max (std::fabs (s), sidePk * peakRelease);

            scopeL[w & (kScopeSize - 1)].store (l[i], std::memory_order_relaxed);
            scopeR[w & (kScopeSize - 1)].store (r[i], std::memory_order_relaxed);
            ++w;
        }
        scopeWrite.store (w, std::memory_order_release);

        const double energy = sxx + syy;
        if (energy > 1.0e-10) // hold the last reading through digital silence
        {
            corr.store ((float) std::clamp (sxy / std::sqrt (sxx * syy + 1.0e-30), -1.0, 1.0));
            balance.store ((float) std::clamp (10.0 * std::log10 ((syy + 1.0e-12) / (sxx + 1.0e-12)), -24.0, 24.0));
            const double stereoPower = energy * 0.5;
            monoLoss.store ((float) std::clamp (10.0 * std::log10 ((smono + 1.0e-12) / (stereoPower + 1.0e-12)), -60.0, 6.0));
        }

        midRms.store (powerDb (smm));
        sideRms.store (powerDb (sss));
        midPeak.store ((float) toDb (midPk));
        sidePeak.store ((float) toDb (sidePk));
    }

    // +1 = mono / in phase, 0 = uncorrelated, -1 = out of phase.
    float correlation() const noexcept   { return corr.load (std::memory_order_relaxed); }
    // dB, positive = right channel louder.
    float balanceDb() const noexcept     { return balance.load (std::memory_order_relaxed); }
    float midRmsDb() const noexcept      { return midRms.load (std::memory_order_relaxed); }
    float sideRmsDb() const noexcept     { return sideRms.load (std::memory_order_relaxed); }
    float midPeakDb() const noexcept     { return midPeak.load (std::memory_order_relaxed); }
    float sidePeakDb() const noexcept    { return sidePeak.load (std::memory_order_relaxed); }
    // Level change when summed to mono, relative to the stereo level: 0 dB = fully mono-safe, -3 dB = uncorrelated.
    float monoLossDb() const noexcept    { return monoLoss.load (std::memory_order_relaxed); }

    // Copies the most recent `count` L/R sample pairs (oldest first) for the goniometer.
    void copyScope (float* l, float* r, int count) const noexcept
    {
        count = std::min (count, kScopeSize);
        const uint32_t w = scopeWrite.load (std::memory_order_acquire);
        for (int i = 0; i < count; ++i)
        {
            const uint32_t idx = (w - (uint32_t) count + (uint32_t) i) & (kScopeSize - 1);
            l[i] = scopeL[idx].load (std::memory_order_relaxed);
            r[i] = scopeR[idx].load (std::memory_order_relaxed);
        }
    }

private:
    static float powerDb (double p) noexcept
    {
        return p > 1.0e-12 ? std::max (kFloor, (float) (10.0 * std::log10 (p))) : kFloor;
    }

    double sampleRate = 48000.0, smooth = 0.999, peakRelease = 0.9999;
    double sxx = 0, syy = 0, sxy = 0, smm = 0, sss = 0, smono = 0, midPk = 0, sidePk = 0;

    std::array<std::atomic<float>, kScopeSize> scopeL, scopeR;
    std::atomic<uint32_t> scopeWrite { 0 };
    std::atomic<float> corr { 0.0f }, balance { 0.0f }, midRms { kFloor }, sideRms { kFloor },
                       midPeak { kFloor }, sidePeak { kFloor }, monoLoss { 0.0f };
};
} // namespace bm
