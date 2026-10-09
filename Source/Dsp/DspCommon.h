#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include "../Meters/Biquad.h"

// Plain C++ DSP building blocks (no JUCE) so every module can be tested with known signals.
namespace bm::dsp
{
inline float dbToLin (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float linToDb (float x) noexcept  { return x > 1.0e-9f ? 20.0f * std::log10 (x) : -180.0f; }

// One-pole smoothing coefficient for a time constant in seconds (0 = instant).
inline double smoothCoef (double seconds, double fs) noexcept
{
    return seconds <= 0.0 ? 0.0 : std::exp (-1.0 / (seconds * fs));
}

inline float flushDenormal (float x) noexcept { return std::fabs (x) < 1.0e-20f ? 0.0f : x; }

// RBJ cookbook biquad designs, normalised so a0 = 1.
struct Coef
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    void applyTo (bm::Biquad& b) const noexcept
    {
        b.b0 = b0; b.b1 = b1; b.b2 = b2; b.a1 = a1; b.a2 = a2;
    }

    // |H(f)| in dB at sample rate fs.
    double magnitudeDb (double f, double fs) const noexcept
    {
        const double w = 2.0 * bm::kPi * f / fs;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
        const auto num = b0 + b1 * z1 + b2 * z2;
        const auto den = 1.0 + a1 * z1 + a2 * z2;
        const double m = std::abs (num) / std::max (1.0e-30, std::abs (den));
        return 20.0 * std::log10 (std::max (1.0e-12, m));
    }
};

inline double clampFreq (double f, double fs) noexcept { return std::clamp (f, 1.0, fs * 0.45); }

inline Coef make (double b0, double b1, double b2, double a0, double a1, double a2) noexcept
{
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

inline Coef lowpass (double fs, double f, double q) noexcept
{
    const double w0 = 2.0 * bm::kPi * clampFreq (f, fs) / fs, c = std::cos (w0), al = std::sin (w0) / (2.0 * q);
    return make ((1 - c) * 0.5, 1 - c, (1 - c) * 0.5, 1 + al, -2 * c, 1 - al);
}

inline Coef highpass (double fs, double f, double q) noexcept
{
    const double w0 = 2.0 * bm::kPi * clampFreq (f, fs) / fs, c = std::cos (w0), al = std::sin (w0) / (2.0 * q);
    return make ((1 + c) * 0.5, -(1 + c), (1 + c) * 0.5, 1 + al, -2 * c, 1 - al);
}

inline Coef allpass (double fs, double f, double q) noexcept
{
    const double w0 = 2.0 * bm::kPi * clampFreq (f, fs) / fs, c = std::cos (w0), al = std::sin (w0) / (2.0 * q);
    return make (1 - al, -2 * c, 1 + al, 1 + al, -2 * c, 1 - al);
}

inline Coef peaking (double fs, double f, double q, double gainDb) noexcept
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = 2.0 * bm::kPi * clampFreq (f, fs) / fs, c = std::cos (w0), al = std::sin (w0) / (2.0 * q);
    return make (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
}

inline Coef lowShelf (double fs, double f, double q, double gainDb) noexcept
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = 2.0 * bm::kPi * clampFreq (f, fs) / fs, c = std::cos (w0), al = std::sin (w0) / (2.0 * q);
    const double sa = 2.0 * std::sqrt (A) * al;
    return make (A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
                 (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
}

inline Coef highShelf (double fs, double f, double q, double gainDb) noexcept
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = 2.0 * bm::kPi * clampFreq (f, fs) / fs, c = std::cos (w0), al = std::sin (w0) / (2.0 * q);
    const double sa = 2.0 * std::sqrt (A) * al;
    return make (A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
                 (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
}

// First-order antiderivative anti-aliasing (ADAA) for a memoryless nonlinearity f with antiderivative F.
// F must return double: the difference of two close F values needs the extra precision.
template <class F, class FInt>
inline float adaa1 (float x, float& xPrev, F f, FInt fInt) noexcept
{
    const double dx = (double) x - (double) xPrev;
    float y;
    if (std::fabs (dx) < 1.0e-5)
        y = f (0.5f * (x + xPrev));
    else
        y = (float) ((fInt (x) - fInt (xPrev)) / dx);
    xPrev = x;
    return y;
}

// ln(cosh(x)), stable for large |x|.
inline double logCosh (double x) noexcept
{
    const double a = std::fabs (x);
    return a + std::log1p (std::exp (-2.0 * a)) - 0.6931471805599453;
}
} // namespace bm::dsp
