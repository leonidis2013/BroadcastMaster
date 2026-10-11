// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>
#include "Biquad.h"

namespace bm
{
// Lock-free single-channel sample ring. The audio thread pushes; the UI thread copies the newest samples
// and runs the FFT, so no FFT work ever happens on the audio thread.
class SampleRing
{
public:
    static constexpr int kSize = 32768;

    SampleRing() { for (auto& s : buf) s.store (0.0f); }

    void push (const float* x, int n) noexcept
    {
        uint32_t w = wr.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
            buf[(w++) & (kSize - 1)].store (x[i], std::memory_order_relaxed);
        wr.store (w, std::memory_order_release);
    }

    void copyLatest (float* dst, int count) const noexcept
    {
        const uint32_t w = wr.load (std::memory_order_acquire);
        for (int i = 0; i < count; ++i)
            dst[i] = buf[(w - (uint32_t) count + (uint32_t) i) & (kSize - 1)].load (std::memory_order_relaxed);
    }

    void clear() noexcept { for (auto& s : buf) s.store (0.0f); }

private:
    std::array<std::atomic<float>, kSize> buf;
    std::atomic<uint32_t> wr { 0 };
};

enum class Weighting { Flat = 0, A = 1, C = 2 };

// IEC 61672 weighting curves, in dB, normalised to 0 dB at 1 kHz.
inline double weightingDb (Weighting w, double f) noexcept
{
    if (w == Weighting::Flat || f <= 0.0) return 0.0;
    const double f2 = f * f;
    const double c1 = 20.6 * 20.6, c4 = 12194.0 * 12194.0;
    if (w == Weighting::A)
    {
        const double ra = c4 * f2 * f2 / ((f2 + c1) * std::sqrt ((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9)) * (f2 + c4));
        return 20.0 * std::log10 (ra) + 2.0;
    }
    const double rc = c4 * f2 / ((f2 + c1) * (f2 + c4));
    return 20.0 * std::log10 (rc) + 0.06;
}

// Radix-2 FFT, split real/imaginary arrays.
class Fft
{
public:
    explicit Fft (int order) : n (1 << order), cosT ((size_t) n / 2), sinT ((size_t) n / 2), rev ((size_t) n)
    {
        for (int i = 0; i < n / 2; ++i)
        {
            cosT[(size_t) i] = (float) std::cos (-2.0 * kPi * i / n);
            sinT[(size_t) i] = (float) std::sin (-2.0 * kPi * i / n);
        }
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < order; ++b)
                if (i & (1 << b)) r |= 1 << (order - 1 - b);
            rev[(size_t) i] = r;
        }
    }

    void forward (float* re, float* im) const noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = rev[(size_t) i];
            if (j > i) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, step = n / len;
            for (int start = 0; start < n; start += len)
                for (int k = 0; k < half; ++k)
                {
                    const float wr = cosT[(size_t) (k * step)], wi = sinT[(size_t) (k * step)];
                    const int a = start + k, b = a + half;
                    const float tr = re[b] * wr - im[b] * wi;
                    const float ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti;
                    re[a] += tr;        im[a] += ti;
                }
        }
    }

    int size() const noexcept { return n; }

private:
    int n;
    std::vector<float> cosT, sinT;
    std::vector<int> rev;
};

// Turns the newest samples of the left and right rings into a fractional-octave spectrum in dBFS
// (a full-scale sine reads 0 dB at its FFT bin). Bands average power density, so pink noise is a -3 dB/oct slope.
class SpectrumAnalyzer
{
public:
    static constexpr int kOrder = 13;
    static constexpr int kFftSize = 1 << kOrder;
    static constexpr float kFloor = -120.0f;

    SpectrumAnalyzer() : fft (kOrder), window ((size_t) kFftSize), re ((size_t) kFftSize), im ((size_t) kFftSize),
                         power ((size_t) kFftSize / 2 + 1)
    {
        double sum = 0.0;
        for (int i = 0; i < kFftSize; ++i)
        {
            window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / kFftSize));
            sum += window[(size_t) i];
        }
        ampScale = (float) (2.0 / sum);
        setBandsPerOctave (6);
    }

    void prepare (double fs) { sampleRate = fs; rebuildBands(); }

    void setBandsPerOctave (int bpo)
    {
        bandsPerOct = std::max (1, bpo);
        rebuildBands();
    }

    int numBands() const noexcept { return (int) centres.size(); }
    double bandCentre (int i) const noexcept { return centres[(size_t) i]; }

    // Fills `out` (size numBands()) with dB values. Returns the average-power FFT result per band.
    void analyse (const SampleRing& left, const SampleRing& right, Weighting weighting, std::vector<float>& out)
    {
        out.assign (centres.size(), kFloor);
        std::fill (power.begin(), power.end(), 0.0f);

        const SampleRing* rings[2] { &left, &right };
        for (const SampleRing* ring : rings)
        {
            ring->copyLatest (re.data(), kFftSize);
            for (int i = 0; i < kFftSize; ++i) { re[(size_t) i] *= window[(size_t) i]; im[(size_t) i] = 0.0f; }
            fft.forward (re.data(), im.data());
            for (int k = 0; k <= kFftSize / 2; ++k)
            {
                const float a = ampScale * std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]);
                power[(size_t) k] += 0.5f * a * a; // average of L and R power
            }
        }

        const double binHz = sampleRate / kFftSize;
        for (size_t b = 0; b < centres.size(); ++b)
        {
            double p = 0.0;
            const int lo = loBin[b], hi = hiBin[b];
            if (hi >= lo)
            {
                for (int k = lo; k <= hi; ++k) p += power[(size_t) k];
                p /= (double) (hi - lo + 1);
            }
            else // band narrower than one bin: interpolate power at the centre frequency
            {
                const double pos = centres[b] / binHz;
                const int k0 = std::clamp ((int) std::floor (pos), 0, kFftSize / 2 - 1);
                const double fr = pos - k0;
                p = power[(size_t) k0] * (1.0 - fr) + power[(size_t) (k0 + 1)] * fr;
            }
            const double db = p > 1.0e-12 ? 10.0 * std::log10 (p) : (double) kFloor;
            out[b] = (float) std::max ((double) kFloor, db + weightingDb (weighting, centres[b]));
        }
    }

    // Single-bin reading, used by the tests to verify calibration.
    float peakBinDb (const SampleRing& left)
    {
        left.copyLatest (re.data(), kFftSize);
        for (int i = 0; i < kFftSize; ++i) { re[(size_t) i] *= window[(size_t) i]; im[(size_t) i] = 0.0f; }
        fft.forward (re.data(), im.data());
        float best = 0.0f;
        for (int k = 1; k < kFftSize / 2; ++k)
            best = std::max (best, ampScale * std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]));
        return best > 1.0e-9f ? 20.0f * std::log10 (best) : kFloor;
    }

private:
    void rebuildBands()
    {
        centres.clear(); loBin.clear(); hiBin.clear();
        const double binHz = sampleRate / kFftSize;
        const double nyq = sampleRate * 0.5;
        // log-spaced centres from exactly 20 Hz to exactly 20 kHz, so the curve fills the whole axis
        const int n = std::max (2, (int) std::lround (bandsPerOct * std::log2 (1000.0)) + 1);
        for (int i = 0; i < n; ++i)
        {
            const double fc = 20.0 * std::pow (1000.0, (double) i / (n - 1));
            if (fc > nyq * 0.98) break;
            const double lo = fc * std::pow (2.0, -0.5 / bandsPerOct);
            const double hi = fc * std::pow (2.0, 0.5 / bandsPerOct);
            centres.push_back (fc);
            loBin.push_back ((int) std::ceil (lo / binHz));
            hiBin.push_back (std::min (kFftSize / 2, (int) std::floor (hi / binHz)));
        }
    }

    Fft fft;
    std::vector<float> window, re, im, power;
    std::vector<double> centres;
    std::vector<int> loBin, hiBin;
    float ampScale = 1.0f;
    double sampleRate = 48000.0;
    int bandsPerOct = 6;
};
} // namespace bm
