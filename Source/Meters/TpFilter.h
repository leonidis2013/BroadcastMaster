#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include "Biquad.h"

namespace bm
{
// Inter-sample peak estimator shared by the true-peak meter and the true-peak limiter.
// 4x oversampling below 96 kHz, 2x below 192 kHz, none above (BS.1770 Annex 2 practice). The interpolation
// filter is a 49-tap (at 4x) Kaiser-windowed sinc whose centre tap lands exactly on the original samples, so
// the estimate is never lower than the sample peak. The estimate for an input sample is available delay()
// samples after that sample arrived.
class TpFilter
{
public:
    static constexpr int kTaps = 12;
    static constexpr int kHistLen = 16, kHistMask = 15;

    // span = filter length in input samples (the meter uses kTaps = 12; the limiter uses a longer, sharper one
    // so content close to Nyquist is not under-read). The history ring must hold span + 1 samples.
    void prepare (double fs, int span = kTaps)
    {
        taps = span;
        factor = fs < 96000.0 ? 4 : (fs < 192000.0 ? 2 : 1);

        const int N = taps * factor + 1;
        const double c = (N - 1) * 0.5;
        const double beta = 8.6;
        std::vector<double> h ((size_t) N);
        double sum = 0.0;

        for (int n = 0; n < N; ++n)
        {
            const double x = (n - c) / (double) factor;
            const double sinc = std::fabs (x) < 1.0e-12 ? 1.0 : std::sin (kPi * x) / (kPi * x);
            const double r = (n - c) / c;
            const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
            h[(size_t) n] = sinc * w;
            sum += h[(size_t) n];
        }

        const double norm = (double) factor / sum;
        coef.assign ((size_t) (factor * (taps + 1)), 0.0f);
        for (int p = 0; p < factor; ++p)
            for (int k = 0; k <= taps; ++k)
            {
                const int idx = k * factor + p;
                if (idx < N)
                    coef[(size_t) (p * (taps + 1) + k)] = (float) (h[(size_t) idx] * norm);
            }

        // Fast path data: per phase, the taps reversed and zero-padded to a multiple of 4, so a dot product with a
        // contiguous window (oldest sample first) needs no index masking and runs four sums in parallel.
        padded = ((taps + 1) + 3) & ~3;
        coefRev.assign ((size_t) (factor * padded), 0.0f);
        lipschitz = 0.0f;
        for (int p = 0; p < factor; ++p)
        {
            float l1 = 0.0f;
            for (int k = 0; k <= taps; ++k)
            {
                const float c = coef[(size_t) (p * (taps + 1) + k)];
                coefRev[(size_t) (p * padded + (padded - 1 - k))] = c;
                l1 += std::fabs (c);
            }
            lipschitz = std::max (lipschitz, l1);
        }
        centreCoef = coef[(size_t) (taps / 2)];   // phase 0: only the centre tap is non-zero
    }

    int oversampling() const noexcept { return factor; }

    // Number of samples a window passed to peakWindow() must hold (the newest sample last).
    int windowLen() const noexcept { return padded; }

    // Upper bound on |interpolated value| / max |input sample| over the filter span: if the loudest recent sample
    // times this is under a ceiling, no inter-sample peak can reach it, so the filter need not run.
    float peakBound() const noexcept { return factor == 1 ? 1.0f : lipschitz; }

    // Same result as peak(), from a contiguous window of windowLen() samples with the newest last.
    float peakWindow (const float* w) const noexcept
    {
        if (factor == 1)
            return std::fabs (w[padded - 1]);

        float best = std::fabs (centreCoef * w[padded - 1 - taps / 2]);   // phase 0 = the sample itself
        for (int p = 1; p < factor; ++p)
        {
            const float* cf = coefRev.data() + p * padded;
            float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
            for (int j = 0; j < padded; j += 4)
            {
                a0 += cf[j] * w[j];
                a1 += cf[j + 1] * w[j + 1];
                a2 += cf[j + 2] * w[j + 2];
                a3 += cf[j + 3] * w[j + 3];
            }
            best = std::max (best, std::fabs ((a0 + a1) + (a2 + a3)));
        }
        return best;
    }

    // Samples between an input sample arriving and its peak estimate being available.
    int delay() const noexcept { return factor == 1 ? 0 : taps / 2; }

    // `hist` is a ring of kHistLen samples with the newest at index `pos`.
    float peak (const float* hist, int pos) const noexcept { return peak (hist, pos, kHistMask); }

    // Same, for a ring whose length is mask + 1.
    float peak (const float* hist, int pos, int mask) const noexcept
    {
        if (factor == 1)
            return std::fabs (hist[pos & mask]);

        float best = 0.0f;
        for (int p = 0; p < factor; ++p)
        {
            const float* cf = coef.data() + p * (taps + 1);
            float y = 0.0f;
            for (int k = 0; k <= taps; ++k)
                y += cf[k] * hist[(pos - k) & mask];
            best = std::max (best, std::fabs (y));
        }
        return best;
    }

private:
    static double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 40; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    }

    int factor = 4, taps = kTaps, padded = 16;
    float lipschitz = 2.0f, centreCoef = 1.0f;
    std::vector<float> coef, coefRev;
};
} // namespace bm
