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
    float gateLufs = -45.0f;     // below this the gain is frozen (so it never pumps up room noise)
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
        smoothE = fastE = activeE = midE = refE = 0.0; frames = 0; refValid = false; quietFrames = 0; boost = 0; catchUp = 0; activeValid = false;
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
    bool  riding() const noexcept       { return ridingOut.load (std::memory_order_relaxed); } // false while holding through a pause

private:
    static constexpr int kRing = 100; // 1 s of 10 ms frames
    static constexpr float kDipAllowanceDb = 6.0f;

    static float toDb (double e) noexcept { return e > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (e)) : -120.0f; }

    void hop (double frameEnergy) noexcept
    {
        const double hopSec = 0.01;
        auto coef = [&] (double tau) { return std::exp (-hopSec / tau); };

        // short smoothers: about 40 ms decides "is someone talking right now", about 100 ms is the peak guard
        const double aS = coef (0.04), aF = coef (0.05);
        smoothE = frames == 0 ? frameEnergy : aS * smoothE + (1.0 - aS) * frameEnergy;
        fastE   = frames == 0 ? frameEnergy : aF * fastE   + (1.0 - aF) * frameEnergy;
        ++frames;
        const float smoothDb = toDb (smoothE), fastDb = toDb (fastE);

        // Activity: a frame counts as programme only if it is above the gate and not far below the running
        // programme level. Pauses and syllable gaps are therefore left out of the loudness measurement and the
        // gain is frozen across them. After a long quiet spell the relative gate is dropped so a much softer
        // speaker can still be picked up (but never room noise, which sits 20 dB or more below the old level).
        bool active = smoothDb > params.gateLufs;
        if (active && activeValid)
        {
            const float actDb = toDb (activeE);
            if (smoothDb < actDb - 15.0f)
                active = quietFrames > 150 && smoothDb > actDb - 20.0f;
        }

        if (active)
        {
            if (! activeValid || quietFrames > 150)
            {
                // fresh start (first audio, or after a long pause): take the new level over quickly
                if (! activeValid || smoothDb < toDb (activeE) - 15.0f || quietFrames > 200)
                {
                    activeE = midE = smoothE;
                    if (quietFrames > 1000) refValid = false; // a whole new programme after a long silence
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
            // slow reference ("where the programme has been for the last several seconds"), used to tell a
            // passing dip or fade from a lasting change of level
            const double aR = coef (8.0);
            refE = refValid ? aR * refE + (1.0 - aR) * frameEnergy : activeE;
            refValid = true;
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
        const float rangeEff = params.rangeDb; (void) p;

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
            // A real jump up in level (the music comes back after a quiet passage, a speaker leans into the mic) is
            // seen right away, ahead of the slow average, so the gain that was raised for the quiet part is taken
            // back at once instead of blasting the output for a second or two.
            const bool jump = fastDb > level + 8.0f;
            // for the next 0.3 s the running level follows the quick one upward, so the gain does not climb again
            if (jump) catchUp = 30;
            if (catchUp > 0) { --catchUp; if (fastE > activeE) activeE = midE = fastE; }
            const float seen = std::max (jump ? fastDb : level, toDb (activeE));
            float desired = std::clamp (params.targetLufs - seen, -rangeEff, rangeEff);
            // A dip or fade well below where the programme has been is only lifted so far (the allowance); if the
            // quiet part lasts, the reference follows it down within about ten seconds and the lift completes.
            const float refDb = toDb (refE);
            if (seen < refDb - 6.0f)
                desired = std::min (desired, std::clamp (params.targetLufs - refDb, -rangeEff, rangeEff) + kDipAllowanceDb);
            const float err = desired - gainDbNow;
            const float startUp = frames < 300 ? 3.0f : 1.0f;          // settle quickly when first started
            if (err > 0.0f)
            {
                // raising the gain is always gentle: lifting a quiet passage too fast just pumps it up
                step = std::min (err, up * scale * startUp * (float) hopSec);
            }
            else
            {
                const float hurry = std::clamp (1.0f + std::fabs (err) / 4.0f, 1.0f, 3.0f);
                float rate = down * scale * hurry;
                const float over = fastDb + gainDbNow - params.targetLufs;      // output running hot
                if (over > 1.5f) rate = std::max (rate, 20.0f * over);          // 3 dB over: 60 dB/s
                step = std::max (err, -rate * (float) hopSec);
            }
        }
        else
        {
            // pause: hold the gain, only pull it inside the range if the range was reduced
            step = std::clamp (gainDbNow, -rangeEff, rangeEff) - gainDbNow;
        }
        gainDbNow += step;
        ridingOut.store (active, std::memory_order_relaxed);
        gainDbOut.store (gainDbNow, std::memory_order_relaxed);

        gStart = gEnd;
        gEnd = dbToLin (gainDbNow);
    }

    double fs = 48000.0, acc = 0.0, smoothE = 0.0, fastE = 0.0, activeE = 0.0, midE = 0.0, refE = 0.0;
    int hopLen = 480, hopCount = 0, frames = 0, ringPos = 0, ringFilled = 0, quietFrames = 0, boost = 0, catchUp = 0;
    bool activeValid = false, refValid = false;
    float gainDbNow = 0.0f, gStart = 1.0f, gEnd = 1.0f, speech = 0.0f;
    float frameRms[kRing] {};
    bm::KWeighting kw[2];
    LevParams params;
    std::atomic<float> gainDbOut { 0.0f }, speechOut { 0.0f }, levelOut { -120.0f };
    std::atomic<bool> ridingOut { false };
};
} // namespace bm::dsp
