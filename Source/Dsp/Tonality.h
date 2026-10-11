// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki
#pragma once
#include "DspCommon.h"

namespace bm::dsp
{
// ---------------------------------------------------------------------------------------------------------
// Saturation: tube (asymmetric, even harmonics), tape (symmetric with a gentle top-end roll-off) and
// transformer (extra low-frequency saturation). First-order ADAA keeps harmonics from aliasing.
// The curves are normalised so small signals pass at unity gain.
enum class SatType { Tube = 0, Tape = 1, Transformer = 2 };

class Saturator
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        dcCoef = (float) std::exp (-2.0 * bm::kPi * 8.0 / fs);
        lpCoef = (float) (1.0 - std::exp (-2.0 * bm::kPi * 16000.0 / fs));
        lowCoef = (float) (1.0 - std::exp (-2.0 * bm::kPi * 150.0 / fs));
        reset();
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
            x1[c] = x2[c] = dcX[c] = dcY[c] = lpState[c] = lowState[c] = 0.0f;
    }

    // amountPct 0-100 sets the drive (0 to +18 dB into the curve); mixPct is the dry/wet blend.
    void setParams (SatType t, float amountPct, float mixPct) noexcept
    {
        type = t;
        const float a = std::clamp (amountPct, 0.0f, 100.0f) * 0.01f;
        active = a > 0.0005f;
        drive = dbToLin (a * 18.0f);
        mix = std::clamp (mixPct, 0.0f, 100.0f) * 0.01f;
    }

    void process (float* l, float* r, int n) noexcept
    {
        if (! active || mix <= 0.0f) return;
        float* ch[2] { l, r };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
                ch[c][i] = ch[c][i] * (1.0f - mix) + mix * shape (c, ch[c][i]);
    }

    float shape (int c, float x) noexcept
    {
        switch (type)
        {
            case SatType::Tube:
            {
                const float u = x * drive;
                const float y = adaa1 (u, x1[c], [] (float v) { return tubeF (v); }, [] (float v) { return tubeInt (v); });
                const float out = y / (drive * kTubeSlope);
                // remove the DC offset the asymmetric curve creates
                const float hp = out - dcX[c] + dcCoef * dcY[c];
                dcX[c] = out; dcY[c] = hp;
                return hp;
            }
            case SatType::Tape:
            {
                const float u = x * drive;
                const float y = adaa1 (u, x1[c], [] (float v) { return std::tanh (v); },
                                       [] (float v) { return logCosh ((double) v); }) / drive;
                lpState[c] += lpCoef * (y - lpState[c]); // gentle high-frequency loss, stronger with more drive
                const float k = std::clamp ((drive - 1.0f) / 7.0f, 0.0f, 1.0f) * 0.5f;
                return y + (lpState[c] - y) * k;
            }
            default: // Transformer: the low band drives the curve harder, like core saturation
            {
                lowState[c] += lowCoef * (x - lowState[c]);
                const float lo = lowState[c], hi = x - lo;
                const float dHi = drive, dLo = drive * 2.0f;
                const float yHi = adaa1 (hi * dHi, x1[c], [] (float v) { return std::tanh (v); },
                                         [] (float v) { return logCosh ((double) v); }) / dHi;
                const float yLo = adaa1 (lo * dLo, x2[c], [] (float v) { return std::tanh (v); },
                                         [] (float v) { return logCosh ((double) v); }) / dLo;
                return yHi + yLo;
            }
        }
    }

private:
    static constexpr float kBias = 0.3f;
    static constexpr float kTubeSlope = 1.0f - 0.2913126f * 0.2913126f; // 1 - tanh(0.3)^2

    static float tubeF (float u) noexcept { return std::tanh (u + kBias) - std::tanh (kBias); }
    static double tubeInt (float u) noexcept
    {
        return logCosh ((double) u + kBias) - (double) u * std::tanh ((double) kBias);
    }

    double fs = 48000.0;
    SatType type = SatType::Tube;
    bool active = false;
    float drive = 1.0f, mix = 1.0f;
    float dcCoef = 0.999f, lpCoef = 0.5f, lowCoef = 0.02f;
    float x1[2] {}, x2[2] {}, dcX[2] {}, dcY[2] {}, lpState[2] {}, lowState[2] {};
};

// ---------------------------------------------------------------------------------------------------------
// Harmonic exciter (adds air above a frequency) and bass enhancer (adds harmonics of the low end so bass
// reads on small speakers). Both add generated harmonics to the untouched signal.
class ToneEnhancer
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        reset();
        setParams (excFreq, excAmount, bassFreq, bassAmount);
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            for (auto& b : excHp1[c]) b.reset();
            for (auto& b : excHp2[c]) b.reset();
            for (auto& b : bassLp[c]) b.reset();
            for (auto& b : bassHp[c]) b.reset();
            for (auto& b : bassLp2[c]) b.reset();
            xp[c] = 0.0f;
        }
    }

    void setParams (float excHz, float excPct, float bassHz, float bassPct)
    {
        excFreq = std::clamp (excHz, 1500.0f, 12000.0f);
        bassFreq = std::clamp (bassHz, 30.0f, 200.0f);
        excAmount = std::clamp (excPct, 0.0f, 100.0f);
        bassAmount = std::clamp (bassPct, 0.0f, 100.0f);
        excGain = excAmount * 0.01f * 0.5f;
        bassGain = bassAmount * 0.01f;

        const Coef h = highpass (fs, excFreq, 0.7071), lp = lowpass (fs, bassFreq, 0.7071),
                   bh = highpass (fs, bassFreq * 2.0, 0.7071), lp2 = lowpass (fs, 700.0, 0.7071);
        for (int c = 0; c < 2; ++c)
        {
            for (auto& b : excHp1[c]) h.applyTo (b);
            for (auto& b : excHp2[c]) h.applyTo (b);
            for (auto& b : bassLp[c]) lp.applyTo (b);
            for (auto& b : bassHp[c]) bh.applyTo (b);
            for (auto& b : bassLp2[c]) lp2.applyTo (b);
        }
    }

    void process (float* l, float* r, int n) noexcept
    {
        const bool exc = excGain > 0.0f, bass = bassGain > 0.0f;
        if (! exc && ! bass) return;

        float* ch[2] { l, r };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float x = ch[c][i];
                float add = 0.0f;

                if (exc)
                {
                    const float hp = (float) excHp1[c][0].process (x) * 4.0f;
                    const float y = adaa1 (hp, xp[c], [] (float v) { return std::tanh (v + 0.2f) - 0.19738f; },
                                           [] (float v) { return logCosh ((double) v + 0.2) - (double) v * 0.19738; });
                    add += excGain * (float) excHp2[c][0].process (y) * 0.25f;
                }

                if (bass)
                {
                    const float lo = (float) bassLp[c][0].process (x) * 3.0f;
                    const float y = std::tanh (lo + 0.3f) - 0.29131f;           // 2nd and 3rd harmonics
                    const float harm = (float) bassLp2[c][0].process (bassHp[c][0].process (y)) * 0.33f;
                    add += bassGain * harm;
                }

                ch[c][i] = x + add;
            }
    }

private:
    double fs = 48000.0;
    float excFreq = 4500.0f, excAmount = 0.0f, bassFreq = 80.0f, bassAmount = 0.0f, excGain = 0.0f, bassGain = 0.0f;
    bm::Biquad excHp1[2][1], excHp2[2][1], bassLp[2][1], bassHp[2][1], bassLp2[2][1];
    float xp[2] {};
};
} // namespace bm::dsp
