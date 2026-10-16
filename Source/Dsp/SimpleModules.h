// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki
#pragma once
#include <vector>
#include "DspCommon.h"

namespace bm::dsp
{
// ---------------------------------------------------------------------------------------------------------
// Subsonic high-pass: 4th-order Butterworth, protects the limiter from inaudible low-frequency energy.
class SubsonicHpf
{
public:
    void prepare (double sampleRate) { fs = sampleRate; reset(); setFrequency (freq); }

    void setFrequency (double f)
    {
        freq = std::clamp (f, 10.0, 60.0);
        const Coef c0 = highpass (fs, freq, 0.5411961001461970), c1 = highpass (fs, freq, 1.3065629648763766);
        for (int ch = 0; ch < 2; ++ch)
        {
            c0.applyTo (st[ch][0]); // applyTo only touches coefficients, so filter state carries over smoothly
            c1.applyTo (st[ch][1]);
        }
    }

    void reset() noexcept { for (auto& c : st) for (auto& b : c) b.reset(); }

    void process (float* l, float* r, int n) noexcept
    {
        float* ch[2] { l, r };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
                ch[c][i] = (float) st[c][1].process (st[c][0].process (ch[c][i]));
    }

private:
    double fs = 48000.0, freq = 25.0;
    bm::Biquad st[2][2];
};

// ---------------------------------------------------------------------------------------------------------
// Phase rotator: a cascade of second-order all-passes. It leaves the magnitude untouched and changes the
// waveform's shape, which lowers the peak of asymmetric voice waveforms so the limiter has more headroom.
class PhaseRotator
{
public:
    static constexpr int kMaxStages = 4;

    void prepare (double sampleRate) { fs = sampleRate; reset(); setParams (freq, stages); }

    void setParams (double f, int numStages)
    {
        freq = std::clamp (f, 40.0, 1000.0);
        stages = std::clamp (numStages, 1, kMaxStages);
        const Coef ap = allpass (fs, freq, 0.5);
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < kMaxStages; ++s)
                ap.applyTo (st[c][s]);
    }

    void reset() noexcept { for (auto& c : st) for (auto& b : c) b.reset(); }

    void process (float* l, float* r, int n) noexcept
    {
        float* ch[2] { l, r };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                double x = ch[c][i];
                for (int s = 0; s < stages; ++s) x = st[c][s].process (x);
                ch[c][i] = (float) x;
            }
    }

private:
    double fs = 48000.0, freq = 160.0;
    int stages = 2;
    bm::Biquad st[2][kMaxStages];
};

// ---------------------------------------------------------------------------------------------------------
// Stereo utilities: polarity, mono sum, width, mid/side gain, and a lip-sync delay.
class StereoUtils
{
public:
    struct Params
    {
        float widthPct = 100.0f;   // 0 = mono, 100 = unchanged, 200 = double side level
        float midGainDb = 0.0f, sideGainDb = 0.0f;
        bool flipL = false, flipR = false, mono = false;
        float delayMs = 0.0f;      // 0 to 500
        float excitePct = 0.0f;    // stereo exciter: harmonics added to the side signal above exciteHz
        float exciteHz = 4000.0f;
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const size_t len = (size_t) (0.5 * fs) + 4096;
        for (auto& d : delayBuf) d.assign (len, 0.0f);
        writePos = 0;
        curDelay = targetDelay = 0;
        fadePos = fadeLen = 0;
        exHp1.reset(); exHp2.reset(); exXp = 0.0f;
        lastExHz = -1.0f;
    }

    // Clears the audio held in the delay line and the exciter filters, without touching the settings or allocating.
    void resetState() noexcept
    {
        for (auto& d : delayBuf) std::fill (d.begin(), d.end(), 0.0f);
        exHp1.reset(); exHp2.reset(); exXp = 0.0f;
    }

    void setParams (const Params& p)
    {
        params = p;
        exGain = std::clamp (p.excitePct, 0.0f, 100.0f) * 0.01f * 0.5f;
        const float hz = std::clamp (p.exciteHz, 1500.0f, 12000.0f);
        if (hz != lastExHz)
        {
            lastExHz = hz;
            const Coef h = highpass (fs, hz, 0.7071);
            h.applyTo (exHp1); h.applyTo (exHp2);
        }
        if (exGain <= 0.0f) { exHp1.reset(); exHp2.reset(); exXp = 0.0f; }
        const int d = (int) std::lround (std::clamp (p.delayMs, 0.0f, 500.0f) * 0.001 * fs);
        if (d != targetDelay)
        {
            fadeFrom = curDelayAtFade();
            targetDelay = d;
            fadePos = 0;
            fadeLen = std::max (1, (int) (0.02 * fs)); // 20 ms crossfade between delay settings
        }
    }

    void process (float* l, float* r, int n) noexcept
    {
        const float mg = dbToLin (params.midGainDb), sg = dbToLin (params.sideGainDb);
        const float wd = std::clamp (params.widthPct, 0.0f, 200.0f) * 0.01f;
        const bool matrix = params.mono || wd != 1.0f || mg != 1.0f || sg != 1.0f;

        for (int i = 0; i < n; ++i)
        {
            float a = params.flipL ? -l[i] : l[i];
            float b = params.flipR ? -r[i] : r[i];

            if (matrix)
            {
                float m = (a + b) * 0.5f, s = (a - b) * 0.5f;
                m *= mg;
                s = params.mono ? 0.0f : s * wd * sg;
                a = m + s;
                b = m - s;
            }

            if (exGain > 0.0f && ! params.mono)
            {
                // stereo exciter: saturate the high-passed side signal and add the harmonics back to it
                const float side = (a - b) * 0.5f;
                const float hp = (float) exHp1.process (side) * 4.0f;
                const float y = adaa1 (hp, exXp, [] (float v) { return std::tanh (v + 0.2f) - 0.19738f; },
                                       [] (float v) { return logCosh ((double) v + 0.2) - (double) v * 0.19738; });
                const float add = exGain * (float) exHp2.process (y) * 0.25f;
                a += add;
                b -= add;
            }

            delayBuf[0][writePos] = a;
            delayBuf[1][writePos] = b;
            float oa, ob;
            readDelayed (oa, ob);
            writePos = (writePos + 1) % delayBuf[0].size();
            l[i] = oa;
            r[i] = ob;
        }
    }

    int currentDelaySamples() const noexcept { return targetDelay; }

private:
    bm::Biquad exHp1, exHp2;
    float exXp = 0.0f, exGain = 0.0f, lastExHz = -1.0f;
    int curDelayAtFade() const noexcept
    {
        if (fadeLen <= 0 || fadePos >= fadeLen) return targetDelay;
        return fadeFrom;
    }

    void readDelayed (float& oa, float& ob) noexcept
    {
        const size_t len = delayBuf[0].size();
        auto tap = [&] (int d, float& a, float& b)
        {
            const size_t idx = (writePos + len - (size_t) d) % len;
            a = delayBuf[0][idx];
            b = delayBuf[1][idx];
        };

        if (fadePos < fadeLen)
        {
            float a0, b0, a1, b1;
            tap (fadeFrom, a0, b0);
            tap (targetDelay, a1, b1);
            const float k = (float) fadePos / (float) fadeLen;
            oa = a0 + (a1 - a0) * k;
            ob = b0 + (b1 - b0) * k;
            if (++fadePos >= fadeLen) { curDelay = targetDelay; fadeFrom = targetDelay; }
        }
        else
            tap (targetDelay, oa, ob);
    }

    Params params;
    double fs = 48000.0;
    std::vector<float> delayBuf[2];
    size_t writePos = 0;
    int curDelay = 0, targetDelay = 0, fadeFrom = 0, fadePos = 0, fadeLen = 0;
};

// ---------------------------------------------------------------------------------------------------------
// Smooth soft clipper: linear up to the knee, then a tanh shoulder that settles exactly on the threshold.
// First-order ADAA keeps the harmonics it creates from aliasing.
class SoftClipper
{
public:
    void prepare (double) { reset(); }
    void reset() noexcept { xPrev[0] = xPrev[1] = 0.0f; }

    // thresholdDb: ceiling of the clipper in dBFS. kneePct: how much of the range stays perfectly linear.
    void setParams (float thresholdDb, float kneePct) noexcept
    {
        c = dbToLin (std::clamp (thresholdDb, -24.0f, 0.0f));
        a = c * std::clamp (kneePct, 0.0f, 95.0f) * 0.01f;
    }

    void process (float* l, float* r, int n) noexcept
    {
        float* ch[2] { l, r };
        for (int k = 0; k < 2; ++k)
            for (int i = 0; i < n; ++i)
                ch[k][i] = adaa1 (ch[k][i], xPrev[k],
                                  [this] (float x) { return f (x); },
                                  [this] (float x) { return F (x); });
    }

    float shape (float x) const noexcept { return f (x); }

private:
    float f (float x) const noexcept
    {
        const float ax = std::fabs (x);
        if (ax <= a) return x;
        const float w = c - a;
        const float y = a + w * std::tanh ((ax - a) / std::max (w, 1.0e-6f));
        return x < 0.0f ? -y : y;
    }

    // Antiderivative of f (even function).
    double F (double x) const noexcept
    {
        const double ax = std::fabs (x);
        if (ax <= a) return 0.5 * ax * ax;
        const double w = std::max ((double) c - a, 1.0e-6);
        return 0.5 * a * a + a * (ax - a) + w * w * logCosh ((ax - a) / w);
    }

    float a = 0.5f, c = 1.0f;
    float xPrev[2] { 0.0f, 0.0f };
};
} // namespace bm::dsp
