// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki
#pragma once
#include <atomic>
#include "DspCommon.h"

namespace bm::dsp
{
enum class LevMode  { Auto = 0, Speech = 1, Music = 2 };
enum class LevSpeed { Slow = 0, Medium = 1, Fast = 2 };

struct LevParams
{
    float targetLufs = -14.0f;
    float rangeDb = 9.0f;        // the leveler never adds or removes more than this
    float gateLufs = -50.0f;     // below this the gain is frozen (so it never pumps up room noise)
    LevSpeed speed = LevSpeed::Medium;
    LevMode mode = LevMode::Auto;
};

// Gain rider: measures the K-weighted loudness of the programme only while someone is actually talking or playing
// (pauses and syllable gaps are left out and the gain is held through them), and moves the gain toward the target
// at a limited rate (dB per second) that speeds up when it is far off, so it follows the programme without
// pumping. A real change of level (talk to music) is followed in about half a second; a loud peak ducks quickly.
// In Auto mode a simple speech/music detector (share of low-energy 10 ms frames, as in Scheirer and Slaney)
// slows the rider down on music so it preserves musical dynamics; Speech and Music force one behaviour.
// This is a heuristic: percussive music can look like speech, which is what the manual modes are for.
class AutoLeveler
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        hopLen = std::max (1, (int) std::lround (fs * 0.01));
        kw[0].prepare (fs);
        kw[1].prepare (fs);
        reset();
    }

    void reset() noexcept
    {
        kw[0].reset(); kw[1].reset();
        acc = 0.0; hopCount = 0;
        smoothE = fastE = activeE = midE = 0.0; frames = 0; quietFrames = 0; boost = 0; activeValid = false;
        gainDbNow = 0.0f;
        gStart = gEnd = 1.0f;
        std::fill (std::begin (frameRms), std::end (frameRms), 0.0f);
        ringPos = 0; ringFilled = 0;
        speech = 0.0f;
        gainDbOut.store (0.0f); speechOut.store (0.0f); levelOut.store (-120.0f);
    }

    void setParams (const LevParams& p) { params = p; }

    void process (float* l, float* r, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const double yl = kw[0].process (l[i]), yr = kw[1].process (r[i]);
            acc += yl * yl + yr * yr;

            const float g = gStart + (gEnd - gStart) * ((float) hopCount / (float) hopLen);
            l[i] *= g;
            r[i] *= g;

            if (++hopCount >= hopLen)
            {
                hop (acc / (double) hopLen);
                acc = 0.0;
                hopCount = 0;
            }
        }
    }

    float gainDb() const noexcept       { return gainDbOut.load (std::memory_order_relaxed); }
    float speechAmount() const noexcept { return speechOut.load (std::memory_order_relaxed); } // 0 music .. 1 speech
    float levelLufs() const noexcept    { return levelOut.load (std::memory_order_relaxed); }

private:
    static constexpr int kRing = 100; // 1 s of 10 ms frames

    static float toDb (double e) noexcept { return e > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (e)) : -120.0f; }

    void hop (double frameEnergy) noexcept
    {
        const double hopSec = 0.01;
        auto coef = [&] (double tau) { return std::exp (-hopSec / tau); };

        // short smoothers: about 40 ms decides "is someone talking right now", about 100 ms is the peak guard
        const double aS = coef (0.04), aF = coef (0.1);
        smoothE = frames == 0 ? frameEnergy : aS * smoothE + (1.0 - aS) * frameEnergy;
        fastE   = frames == 0 ? frameEnergy : aF * fastE   + (1.0 - aF) * frameEnergy;
        ++frames;
        const float smoothDb = toDb (smoothE), fastDb = toDb (fastE);

        // Activity: a frame counts as programme only if it is above the gate and not far below the running
        // programme level. Pauses and syllable gaps are therefore left out of the loudness measurement and the
        // gain is frozen across them. After a long quiet spell the relative gate is dropped so a much softer
        // speaker can still be picked up (but never room noise, which sits far below the old level).
        bool active = smoothDb > params.gateLufs;
        if (active && activeValid)
        {
            const float actDb = toDb (activeE);
            if (smoothDb < actDb - 15.0f)
                active = quietFrames > 150 && smoothDb > actDb - 30.0f;
        }

        if (active)
        {
            if (! activeValid || quietFrames > 150)
            {
                // fresh start (first audio, or after a long pause): take the new level over quickly
                if (! activeValid || smoothDb < toDb (activeE) - 15.0f || quietFrames > 200)
                {
                    activeE = midE = smoothE;
                    boost = 40;
                }
                activeValid = true;
            }
            quietFrames = 0;

            const double aM = coef (0.4);
            midE = aM * midE + (1.0 - aM) * frameEnergy;
            const float midDb = toDb (midE), actDb = toDb (activeE);
            // a real change of level (talk to music, band starts) is followed in about half a second
            const double tau = boost > 0 ? 0.15 : (std::fabs (midDb - actDb) > 5.0f ? 0.4 : 1.2);
            if (boost > 0) --boost;
            const double aA = coef (tau);
            activeE = aA * activeE + (1.0 - aA) * frameEnergy;
        }
        else
            ++quietFrames;

        const float level = activeValid ? toDb (activeE) : -120.0f;
        levelOut.store (level, std::memory_order_relaxed);

        // speech / music detector (a pause counts as a low-energy frame, which is what marks speech)
        frameRms[ringPos] = (float) std::sqrt (frameEnergy);
        ringPos = (ringPos + 1) % kRing;
        ringFilled = std::min (ringFilled + 1, kRing);
        if (ringFilled >= kRing)
        {
            double mean = 0.0;
            for (float v : frameRms) mean += v;
            mean /= kRing;
            if (mean > 1.0e-5)
            {
                int low = 0;
                for (float v : frameRms) if (v < 0.5 * mean) ++low;
                const float target = (float) low / (float) kRing > 0.22f ? 1.0f : 0.0f;
                speech += (target - speech) * (float) (1.0 - std::exp (-hopSec / 3.0));
            }
        }
        speechOut.store (speech, std::memory_order_relaxed);

        // gain riding
        const float p = params.mode == LevMode::Speech ? 1.0f : (params.mode == LevMode::Music ? 0.0f : speech);
        const float scale = 0.5f + 0.5f * p;
        const float rangeEff = params.rangeDb * (0.75f + 0.25f * p);

        float up, down;
        switch (params.speed)
        {
            case LevSpeed::Slow:   up = 2.0f; down = 5.0f;  break;
            case LevSpeed::Fast:   up = 8.0f; down = 20.0f; break;
            default:               up = 4.0f; down = 10.0f; break;
        }

        float step = 0.0f;
        if (active)
        {
            // loud bursts (a speaker peaking) count a little ahead of the slow average
            const float seen = std::max (level, fastDb - 4.0f);
            const float desired = std::clamp (params.targetLufs - seen, -rangeEff, rangeEff);
            const float err = desired - gainDbNow;
            const float boostRate = std::clamp (1.0f + std::fabs (err) / 4.0f, 1.0f, 3.0f); // far from target: hurry
            const bool over = fastDb + gainDbNow > params.targetLufs + 2.5f;                  // output running hot
            if (err > 0.0f)
                step = std::min (err, up * scale * boostRate * (float) hopSec);
            else
                step = std::max (err, -down * scale * boostRate * (over ? 2.5f : 1.0f) * (float) hopSec);
        }
        else
        {
            // pause: hold the gain, only pull it inside the range if the range was reduced
            step = std::clamp (gainDbNow, -rangeEff, rangeEff) - gainDbNow;
        }
        gainDbNow += step;
        gainDbOut.store (gainDbNow, std::memory_order_relaxed);

        gStart = gEnd;
        gEnd = dbToLin (gainDbNow);
    }

    double fs = 48000.0, acc = 0.0, smoothE = 0.0, fastE = 0.0, activeE = 0.0, midE = 0.0;
    int hopLen = 480, hopCount = 0, frames = 0, ringPos = 0, ringFilled = 0, quietFrames = 0, boost = 0;
    bool activeValid = false;
    float gainDbNow = 0.0f, gStart = 1.0f, gEnd = 1.0f, speech = 0.0f;
    float frameRms[kRing] {};
    bm::KWeighting kw[2];
    LevParams params;
    std::atomic<float> gainDbOut { 0.0f }, speechOut { 0.0f }, levelOut { -120.0f };
};
} // namespace bm::dsp
