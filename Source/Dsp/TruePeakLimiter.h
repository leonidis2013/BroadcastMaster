#pragma once
#include <atomic>
#include <vector>
#include "DspCommon.h"
#include "../Meters/TpFilter.h"

namespace bm::dsp
{
// Look-ahead brick-wall limiter that works on the oversampled (true) peak.
//
// For every input sample it finds the gain needed to keep the inter-sample peak under the ceiling, takes the
// sliding minimum of that over the look-ahead window and then a moving average over the same window. The
// average of the minima is never above the requirement of any sample in the window, so no peak gets through,
// and the gain moves smoothly instead of clicking. A release envelope stretches the recovery.
// The audio is delayed by delay() + lookahead - 1 samples (see latency()).
class TruePeakLimiter
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        filter.prepare (fs, kSpan);
        maxLook = std::max (8, (int) (0.012 * fs) + 8);
        const size_t cap = (size_t) (maxLook + filter.delay() + 16);
        for (auto& d : delayBuf) d.assign (cap, 0.0f);
        dqIdx.assign ((size_t) maxLook + 2, 0);
        dqVal.assign ((size_t) maxLook + 2, 1.0f);
        mRing.assign ((size_t) maxLook + 2, 1.0f);
        setParams (ceilingDb, releaseMs, lookMs, enabled);
        reset();
    }

    void setParams (float ceilDb, float relMs, float lookaheadMs, bool on)
    {
        ceilingDb = std::clamp (ceilDb, -24.0f, 0.0f);
        releaseMs = std::clamp (relMs, 5.0f, 2000.0f);
        enabled = on;
        const int newL = std::clamp ((int) std::lround (std::max (0.2f, lookaheadMs) * 0.001 * fs), 4, maxLook);
        lookMs = lookaheadMs;
        ceilLin = std::pow (10.0f, (ceilingDb - 0.3f) * 0.05f); // 0.3 dB margin: the estimate can under-read content near Nyquist
        relCoef = (float) smoothCoef (releaseMs * 0.001, fs);
        if (newL != L) { L = newL; reset(); }
    }

    void reset() noexcept
    {
        for (auto& d : delayBuf) std::fill (d.begin(), d.end(), 0.0f);
        for (auto& h : hist) std::fill (std::begin (h), std::end (h), 0.0f);
        histPos = 0;
        wp = 0;
        t = 0;
        head = tail = 0;
        std::fill (mRing.begin(), mRing.end(), 1.0f);
        mPos = 0;
        sum = (double) L;
        env = 1.0f;
        grOut.store (0.0f);
    }

    // Samples of delay added to the audio.
    int latency() const noexcept { return filter.delay() + L - 1; }

    // In place. If deltaL/deltaR are given they receive (delayed input - output): what the limiter removed.
    void process (float* l, float* r, int n, float* deltaL = nullptr, float* deltaR = nullptr) noexcept
    {
        const int D = latency();
        const size_t cap = delayBuf[0].size();
        float minEnv = 1.0f;
        float* io[2] { l, r };

        for (int i = 0; i < n; ++i)
        {
            float tp = 0.0f;
            for (int c = 0; c < 2; ++c)
            {
                hist[c][histPos] = io[c][i];
                tp = std::max (tp, filter.peak (hist[c], histPos, kHistMask));
                delayBuf[c][wp] = io[c][i];
            }
            histPos = (histPos + 1) & kHistMask;

            const float req = (enabled && tp > ceilLin) ? ceilLin / tp : 1.0f;

            // sliding minimum over the last L requirements (monotonic queue)
            while (tail > head && dqVal[(size_t) ((tail - 1) % qCap())] >= req) --tail;
            dqIdx[(size_t) (tail % qCap())] = t;
            dqVal[(size_t) (tail % qCap())] = req;
            ++tail;
            while (dqIdx[(size_t) (head % qCap())] <= t - L) ++head;
            const float m = dqVal[(size_t) (head % qCap())];

            // moving average of the minima over L samples
            sum += (double) m - (double) mRing[(size_t) mPos];
            mRing[(size_t) mPos] = m;
            mPos = (mPos + 1) % L;
            const float G = (float) (sum / (double) L);

            env = G <= env ? G : env + (G - env) * (1.0f - relCoef);
            minEnv = std::min (minEnv, env);

            const size_t rp = (wp + cap - (size_t) D) % cap;
            for (int c = 0; c < 2; ++c)
            {
                const float dly = delayBuf[c][rp];
                const float y = dly * env;
                if (c == 0 && deltaL) deltaL[i] = dly - y;
                if (c == 1 && deltaR) deltaR[i] = dly - y;
                io[c][i] = y;
            }

            wp = (wp + 1) % cap;
            ++t;
        }

        grOut.store (-linToDb (minEnv), std::memory_order_relaxed);
    }

    // Largest gain reduction (dB, positive) in the last block.
    float grDb() const noexcept { return grOut.load (std::memory_order_relaxed); }

private:
    size_t qCap() const noexcept { return dqIdx.size(); }

    double fs = 48000.0;
    TpFilter filter;
    int maxLook = 600, L = 144;
    float ceilingDb = -1.0f, releaseMs = 120.0f, lookMs = 3.0f, ceilLin = 0.88f, relCoef = 0.999f;
    bool enabled = true;

    std::vector<float> delayBuf[2];
    static constexpr int kSpan = 48, kHistLen = 64, kHistMask = 63; // sharper filter than the meter's: see TpFilter
    float hist[2][kHistLen] {};
    int histPos = 0;
    size_t wp = 0;

    long long t = 0;
    std::vector<long long> dqIdx;
    std::vector<float> dqVal, mRing;
    long long head = 0, tail = 0;
    int mPos = 0;
    double sum = 0.0;
    float env = 1.0f;
    std::atomic<float> grOut { 0.0f };
};
} // namespace bm::dsp
