#pragma once
#include <cmath>

// Small, JUCE-free DSP building blocks so the metering engine can be unit tested on its own.
namespace bm
{
constexpr double kPi = 3.14159265358979323846;

// Transposed direct form II biquad, double precision (needed for the 38 Hz K-weighting stage).
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() noexcept { z1 = z2 = 0.0; }
};

// ITU-R BS.1770-4 K-weighting (stage 1 high shelf + stage 2 high pass), valid at any sample rate.
struct KWeighting
{
    Biquad shelf, highpass;

    void prepare (double fs)
    {
        {
            const double f0 = 1681.974450955533;
            const double G  = 3.999843853973347;
            const double Q  = 0.7071752369554196;
            const double K  = std::tan (kPi * f0 / fs);
            const double Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
            shelf.b1 = 2.0 * (K * K - Vh) / a0;
            shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
            shelf.a1 = 2.0 * (K * K - 1.0) / a0;
            shelf.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {
            const double f0 = 38.13547087602444;
            const double Q  = 0.5003270373238773;
            const double K  = std::tan (kPi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            highpass.b0 = 1.0;
            highpass.b1 = -2.0;
            highpass.b2 = 1.0;
            highpass.a1 = 2.0 * (K * K - 1.0) / a0;
            highpass.a2 = (1.0 - K / Q + K * K) / a0;
        }
        reset();
    }

    inline double process (double x) noexcept { return highpass.process (shelf.process (x)); }
    void reset() noexcept { shelf.reset(); highpass.reset(); }
};

inline double toDb (double linear, double floorDb = -120.0) noexcept
{
    return linear > 1.0e-12 ? std::max (floorDb, 20.0 * std::log10 (linear)) : floorDb;
}
} // namespace bm
