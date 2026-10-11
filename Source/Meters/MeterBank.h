// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include "LoudnessMeter.h"
#include "TruePeakMeter.h"
#include "StereoMeter.h"
#include "SpectrumAnalyzer.h"

namespace bm
{
// Crest factor over a sliding 3 s window (sample peak vs RMS, unweighted).
class CrestMeter
{
public:
    static constexpr float kFloor = -120.0f;

    void prepare (double fs)
    {
        subBlockLen = std::max (1, (int) std::lround (fs * 0.1));
        std::fill (std::begin (sumSq), std::end (sumSq), 0.0);
        std::fill (std::begin (peak), std::end (peak), 0.0f);
        pos = count = 0; acc = 0.0; pk = 0.0f; filled = 0;
        crest.store (0.0f);
    }

    void process (const float* l, const float* r, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            acc += (double) l[i] * l[i] + (double) r[i] * r[i];
            pk = std::max ({ pk, std::fabs (l[i]), std::fabs (r[i]) });
            if (++count >= subBlockLen)
            {
                sumSq[pos] = acc; peak[pos] = pk;
                pos = (pos + 1) % kRing; filled = std::min (filled + 1, kRing);
                acc = 0.0; pk = 0.0f; count = 0;

                double s = 0.0; float p = 0.0f;
                for (int k = 0; k < filled; ++k) { s += sumSq[k]; p = std::max (p, peak[k]); }
                const double rms = std::sqrt (s / (2.0 * subBlockLen * filled));
                if (rms > 1.0e-6 && p > 1.0e-6f)
                    crest.store ((float) (20.0 * std::log10 (p / rms)), std::memory_order_relaxed);
            }
        }
    }

    float crestDb() const noexcept { return crest.load (std::memory_order_relaxed); }

private:
    static constexpr int kRing = 30;
    int subBlockLen = 4800, pos = 0, count = 0, filled = 0;
    double acc = 0.0, sumSq[kRing] {};
    float pk = 0.0f, peak[kRing] {};
    std::atomic<float> crest { 0.0f };
};

// Everything the UI shows, read from one MeterBank in one go.
struct MeterSnapshot
{
    float momentary = -120.0f, shortTerm = -120.0f, integrated = -120.0f, lra = 0.0f;
    float maxMomentary = -120.0f, maxShortTerm = -120.0f;
    float tpRecent = -120.0f, tpHold = -120.0f, samplePeak = -120.0f;
    float correlation = 0.0f, balanceDb = 0.0f, monoLossDb = 0.0f;
    float midRms = -120.0f, sideRms = -120.0f, midPeak = -120.0f, sidePeak = -120.0f;
    float crest = 0.0f;
    float psr = 0.0f; // peak-to-short-term loudness ratio, dB
    float plr = 0.0f; // peak-to-integrated loudness ratio, dB
    bool hasPsr = false, hasPlr = false;
};

// One complete set of meters for one point in the chain. The processor owns two: chain input and chain output.
struct MeterBank
{
    LoudnessMeter loudness;
    TruePeakMeter truePeak;
    StereoMeter stereo;
    CrestMeter crest;
    SampleRing ringL, ringR;

    void prepare (double fs)
    {
        loudness.prepare (fs);
        truePeak.prepare (fs);
        stereo.prepare (fs);
        crest.prepare (fs);
        ringL.clear();
        ringR.clear();
    }

    void process (const float* l, const float* r, int n) noexcept
    {
        loudness.process (l, r, n);
        truePeak.process (l, r, n);
        stereo.process (l, r, n);
        crest.process (l, r, n);
        ringL.push (l, n);
        ringR.push (r, n);
    }

    void requestReset() noexcept
    {
        loudness.requestReset();
        truePeak.requestReset();
    }

    MeterSnapshot snapshot() const noexcept
    {
        MeterSnapshot s;
        s.momentary = loudness.momentary();
        s.shortTerm = loudness.shortTerm();
        s.integrated = loudness.integrated();
        s.lra = loudness.range();
        s.maxMomentary = loudness.maxMomentary();
        s.maxShortTerm = loudness.maxShortTerm();
        s.tpRecent = truePeak.recentDb();
        s.tpHold = truePeak.holdDb();
        s.samplePeak = truePeak.samplePeakDb();
        s.correlation = stereo.correlation();
        s.balanceDb = stereo.balanceDb();
        s.monoLossDb = stereo.monoLossDb();
        s.midRms = stereo.midRmsDb();
        s.sideRms = stereo.sideRmsDb();
        s.midPeak = stereo.midPeakDb();
        s.sidePeak = stereo.sidePeakDb();
        s.crest = crest.crestDb();

        const float tpWin = truePeak.window3sDb();
        if (tpWin > -119.0f && s.shortTerm > -119.0f) { s.psr = tpWin - s.shortTerm; s.hasPsr = true; }
        if (s.tpHold > -119.0f && s.integrated > -119.0f) { s.plr = s.tpHold - s.integrated; s.hasPlr = true; }
        return s;
    }
};
} // namespace bm
