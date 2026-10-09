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
    }

    int oversampling() const noexcept { return factor; }

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

    int factor = 4, taps = kTaps;
    std::vector<float> coef;
};
} // namespace bm
