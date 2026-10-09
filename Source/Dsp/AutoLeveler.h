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

// Slow gain rider: measures K-weighted loudness of its input over about a second and moves the gain toward
// the target at a limited rate (dB per second), so it follows the programme without pumping.
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
        energyAvg = 0.0; fastAvg = 0.0; frames = 0;
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

    void hop (double frameEnergy) noexcept
    {
        const double hopSec = 0.01;

        // loudness estimate: exponential average, quick to settle during the first seconds
        const double tau = frames < 200 ? 0.3 : 1.2;
        const double a = std::exp (-hopSec / tau);
        energyAvg = frames == 0 ? frameEnergy : a * energyAvg + (1.0 - a) * frameEnergy;
        ++frames;
        const float level = energyAvg > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (energyAvg)) : -120.0f;
        levelOut.store (level, std::memory_order_relaxed);

        // momentary level (about 100 ms): used to freeze the gain at once when the programme stops or pauses,
        // long before the slow average has decayed
        const double aF = std::exp (-hopSec / 0.1);
        fastAvg = frames == 1 ? frameEnergy : aF * fastAvg + (1.0 - aF) * frameEnergy;
        const float fast = fastAvg > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (fastAvg)) : -120.0f;
        const bool hold = fast < params.gateLufs || fast < level - 9.0f;

        // speech / music detector
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
                speech += (target - speech) * (float) (1.0 - std::exp (-hopSec / 4.0));
            }
        }
        speechOut.store (speech, std::memory_order_relaxed);

        // gain riding
        const float p = params.mode == LevMode::Speech ? 1.0f : (params.mode == LevMode::Music ? 0.0f : speech);
        const float scale = 0.35f + 0.65f * p;
        const float rangeEff = params.rangeDb * (0.5f + 0.5f * p);

        float up, down;
        switch (params.speed)
        {
            case LevSpeed::Slow:   up = 1.5f; down = 4.0f;  break;
            case LevSpeed::Fast:   up = 6.0f; down = 15.0f; break;
            default:               up = 3.0f; down = 8.0f;  break;
        }

        float desired = gainDbNow;
        if (! hold && level > params.gateLufs)
            desired = std::clamp (params.targetLufs - level, -rangeEff, rangeEff);
        else
            desired = std::clamp (desired, -rangeEff, rangeEff);

        const float diff = desired - gainDbNow;
        const float step = diff > 0.0f ? std::min (diff, up * scale * (float) hopSec) : std::max (diff, -down * scale * (float) hopSec);
        gainDbNow += step;
        gainDbOut.store (gainDbNow, std::memory_order_relaxed);

        gStart = gEnd;
        gEnd = dbToLin (gainDbNow);
    }

    double fs = 48000.0, acc = 0.0, energyAvg = 0.0, fastAvg = 0.0;
    int hopLen = 480, hopCount = 0, frames = 0, ringPos = 0, ringFilled = 0;
    float gainDbNow = 0.0f, gStart = 1.0f, gEnd = 1.0f, speech = 0.0f;
    float frameRms[kRing] {};
    bm::KWeighting kw[2];
    LevParams params;
    std::atomic<float> gainDbOut { 0.0f }, speechOut { 0.0f }, levelOut { -120.0f };
};
} // namespace bm::dsp
