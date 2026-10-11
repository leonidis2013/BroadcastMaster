#pragma once
#include <cstring>
#include <vector>
#include "DspCommon.h"
#include "../Meters/SpectrumAnalyzer.h" // bm::Fft

namespace bm::dsp
{
struct EqBand
{
    bool on = true;
    float freq = 1000.0f, gainDb = 0.0f, q = 1.0f;
    int target = 0; // 0 = stereo (mid and side), 1 = mid only, 2 = side only
};

// Six bands: 1 = low shelf, 2-5 = peaking, 6 = high shelf. Tilt is a broad see-saw around 1 kHz.
struct EqParams
{
    static constexpr int kBands = 6;
    bool enabled = true;
    EqBand band[kBands];
    float tiltDb = 0.0f;

    EqParams()
    {
        const float f[kBands] { 80.0f, 250.0f, 1000.0f, 3500.0f, 8000.0f, 12000.0f };
        const float q[kBands] { 0.7f, 1.0f, 1.0f, 1.0f, 1.0f, 0.7f };
        for (int i = 0; i < kBands; ++i) { band[i].freq = f[i]; band[i].q = q[i]; }
    }

    bool operator== (const EqParams& o) const noexcept
    {
        if (enabled != o.enabled || tiltDb != o.tiltDb) return false;
        for (int i = 0; i < kBands; ++i)
            if (band[i].on != o.band[i].on || band[i].freq != o.band[i].freq || band[i].gainDb != o.band[i].gainDb
                || band[i].q != o.band[i].q || band[i].target != o.band[i].target)
                return false;
        return true;
    }
    bool operator!= (const EqParams& o) const noexcept { return ! (*this == o); }
};

// The filters of the mid chain and side chain for a parameter set.
struct EqChains
{
    static constexpr int kMax = EqParams::kBands + 2;
    Coef mid[kMax], side[kMax];
    int nMid = 0, nSide = 0;

    static EqChains build (const EqParams& p, double fs)
    {
        EqChains c;
        if (! p.enabled) return c;

        auto add = [&] (const Coef& k, bool toMid, bool toSide)
        {
            if (toMid) c.mid[c.nMid++] = k;
            if (toSide) c.side[c.nSide++] = k;
        };

        for (int i = 0; i < EqParams::kBands; ++i)
        {
            const auto& b = p.band[i];
            if (! b.on || std::fabs (b.gainDb) < 0.01f) continue;
            const Coef k = i == 0 ? lowShelf (fs, b.freq, b.q, b.gainDb)
                         : i == EqParams::kBands - 1 ? highShelf (fs, b.freq, b.q, b.gainDb)
                                                      : peaking (fs, b.freq, b.q, b.gainDb);
            add (k, b.target != 2, b.target != 1);
        }

        if (std::fabs (p.tiltDb) >= 0.01f) // low shelf down + high shelf up = tilt around about 1 kHz
        {
            add (lowShelf (fs, 500.0, 0.5, -0.5 * p.tiltDb), true, true);
            add (highShelf (fs, 2000.0, 0.5, 0.5 * p.tiltDb), true, true);
        }
        return c;
    }
};

// Total magnitude of one chain, in dB, at frequency f (used for the UI curve).
inline double chainResponseDb (const Coef* c, int n, double f, double fs) noexcept
{
    double db = 0.0;
    for (int i = 0; i < n; ++i) db += c[i].magnitudeDb (f, fs);
    return db;
}

// Frequency-domain kernel for the streaming convolver: N-point complex spectrum.
struct KernelSpectrum
{
    std::vector<float> re, im;
};

struct KernelPair
{
    KernelSpectrum mid, side;
    bool flat = false;   // pure delay (no EQ): the convolver then copies samples instead of running FFTs
};

inline constexpr int kFftN = 4096;        // convolver FFT size
inline constexpr int kHop = 256;          // samples per block; adds kHop samples of latency
inline constexpr int kCentre = 1024;      // linear-phase kernel is 2*kCentre+1 taps, delay = kCentre
inline constexpr int kEqLatency = kCentre + kHop;
static_assert (kFftN == (1 << 12), "convolver FFT order");

// Builds a linear-phase kernel with the magnitude response of an IIR chain (zero phase, rotated to the centre).
inline void designKernel (const Coef* chain, int n, double fs, KernelSpectrum& out, const bm::Fft& fft)
{
    std::vector<float> re ((size_t) kFftN), im ((size_t) kFftN, 0.0f);

    for (int k = 0; k <= kFftN / 2; ++k)
    {
        const double w = 2.0 * bm::kPi * (double) k / kFftN;
        const double c1 = std::cos (w), c2 = std::cos (2.0 * w);
        double mag2 = 1.0;
        for (int i = 0; i < n; ++i)
        {
            const auto& q = chain[i];
            const double num = q.b0 * q.b0 + q.b1 * q.b1 + q.b2 * q.b2 + 2.0 * (q.b0 * q.b1 + q.b1 * q.b2) * c1 + 2.0 * q.b0 * q.b2 * c2;
            const double den = 1.0 + q.a1 * q.a1 + q.a2 * q.a2 + 2.0 * (q.a1 + q.a1 * q.a2) * c1 + 2.0 * q.a2 * c2;
            mag2 *= num / std::max (1.0e-30, den);
        }
        const float mag = (float) std::sqrt (std::max (0.0, mag2));
        re[(size_t) k] = mag;
        if (k > 0 && k < kFftN / 2) re[(size_t) (kFftN - k)] = mag;
    }
    (void) fs;

    fft.forward (re.data(), im.data()); // real, even spectrum: forward FFT equals inverse FFT times N
    std::vector<float> h ((size_t) kFftN, 0.0f);
    const double invN = 1.0 / kFftN;
    for (int n2 = 0; n2 <= 2 * kCentre; ++n2)
    {
        const double x = (double) n2 / (2.0 * kCentre);
        const double win = 0.42 - 0.5 * std::cos (2.0 * bm::kPi * x) + 0.08 * std::cos (4.0 * bm::kPi * x);
        h[(size_t) n2] = (float) (re[(size_t) ((n2 - kCentre + kFftN) % kFftN)] * invN * win);
    }

    out.re = h;
    out.im.assign ((size_t) kFftN, 0.0f);
    fft.forward (out.re.data(), out.im.data());
}

inline void designKernels (const EqParams& p, double fs, KernelPair& out, const bm::Fft& fft)
{
    const EqChains c = EqChains::build (p, fs);
    designKernel (c.mid, c.nMid, fs, out.mid, fft);
    designKernel (c.side, c.nSide, fs, out.side, fft);
    out.flat = c.nMid == 0 && c.nSide == 0;
}

// ---------------------------------------------------------------------------------------------------------
// Streaming overlap-save FIR convolver for two channels with a crossfade whenever the kernels change.
class TwoChannelConvolver
{
public:
    TwoChannelConvolver() : fft (12) // 2^12 = kFftN
    {
        for (int c = 0; c < 2; ++c)
        {
            inBuf[c].assign ((size_t) kFftN, 0.0f);
            acc[c].assign ((size_t) kHop, 0.0f);
            outBlock[c].assign ((size_t) kHop, 0.0f);
        }
        scratchRe.assign ((size_t) kFftN, 0.0f);
        scratchIm.assign ((size_t) kFftN, 0.0f);
        xRe.assign ((size_t) kFftN, 0.0f);
        xIm.assign ((size_t) kFftN, 0.0f);
        oldBlock.assign ((size_t) kHop, 0.0f);
        oldBlock2.assign ((size_t) kHop, 0.0f);
        setFlat();
    }

    const bm::Fft& fftEngine() const noexcept { return fft; }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (inBuf[c].begin(), inBuf[c].end(), 0.0f);
            std::fill (acc[c].begin(), acc[c].end(), 0.0f);
            std::fill (outBlock[c].begin(), outBlock[c].end(), 0.0f);
        }
        accCount = 0;
        fading = false;
    }

    // Pure delay of kEqLatency samples.
    void setFlat()
    {
        KernelPair flat;
        for (auto* k : { &flat.mid, &flat.side })
        {
            k->re.assign ((size_t) kFftN, 0.0f);
            k->im.assign ((size_t) kFftN, 0.0f);
            std::vector<float> h ((size_t) kFftN, 0.0f);
            h[(size_t) kCentre] = 1.0f;
            fft.forward (h.data(), k->im.data());
            k->re = h;
        }
        flat.flat = true;
        setKernels (flat, true);
    }

    void setKernels (const KernelPair& k, bool immediate = false)
    {
        if (immediate || ! haveKernels)
        {
            cur = k; prev = k; fading = false; haveKernels = true;
            return;
        }
        prev = cur;
        cur = k;
        fading = true;
    }

    // In-place on two channels (the caller passes mid and side).
    void process (float* c0, float* c1, int n) noexcept
    {
        float* io[2] { c0, c1 };
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                acc[c][(size_t) accCount] = io[c][i];
                io[c][i] = outBlock[c][(size_t) accCount];
            }
            if (++accCount >= kHop)
            {
                accCount = 0;
                runHop();
            }
        }
    }

private:
    // One hop for both channels. Mid and side are real signals, so they share ONE complex FFT (mid in the real
    // part, side in the imaginary part) and one inverse FFT. A flat kernel (EQ off or neutral) needs no FFT at all:
    // the output is just the input delayed by kCentre samples.
    void runHop() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            auto& buf = inBuf[c];
            std::memmove (buf.data(), buf.data() + kHop, (size_t) (kFftN - kHop) * sizeof (float));
            std::memcpy (buf.data() + kFftN - kHop, acc[c].data(), (size_t) kHop * sizeof (float));
        }

        if (! cur.flat || (fading && ! prev.flat))
        {
            std::memcpy (xRe.data(), inBuf[0].data(), (size_t) kFftN * sizeof (float));
            std::memcpy (xIm.data(), inBuf[1].data(), (size_t) kFftN * sizeof (float));
            fft.forward (xRe.data(), xIm.data());
        }

        block (cur, outBlock[0].data(), outBlock[1].data());
        if (fading)
        {
            block (prev, oldBlock.data(), oldBlock2.data());
            for (int i = 0; i < kHop; ++i)
            {
                const float g = (float) (i + 1) / (float) kHop;
                outBlock[0][(size_t) i] = oldBlock[(size_t) i] + (outBlock[0][(size_t) i] - oldBlock[(size_t) i]) * g;
                outBlock[1][(size_t) i] = oldBlock2[(size_t) i] + (outBlock[1][(size_t) i] - oldBlock2[(size_t) i]) * g;
            }
        }
        fading = false;
    }

    void block (const KernelPair& k, float* dstM, float* dstS) noexcept
    {
        if (k.flat)
        {
            std::memcpy (dstM, inBuf[0].data() + (kFftN - kHop - kCentre), (size_t) kHop * sizeof (float));
            std::memcpy (dstS, inBuf[1].data() + (kFftN - kHop - kCentre), (size_t) kHop * sizeof (float));
            return;
        }

        const float *kmr = k.mid.re.data(), *kmi = k.mid.im.data(), *ksr = k.side.re.data(), *ksi = k.side.im.data();
        for (int i = 0; i < kFftN; ++i)
        {
            const int j = (kFftN - i) & (kFftN - 1);
            const float ar = xRe[(size_t) i], ai = xIm[(size_t) i], br = xRe[(size_t) j], bi = xIm[(size_t) j];
            const float mr = 0.5f * (ar + br), mi = 0.5f * (ai - bi);     // spectrum of the mid input
            const float sr = 0.5f * (ai + bi), si = -0.5f * (ar - br);    // spectrum of the side input
            const float ymr = mr * kmr[i] - mi * kmi[i], ymi = mr * kmi[i] + mi * kmr[i];
            const float ysr = sr * ksr[i] - si * ksi[i], ysi = sr * ksi[i] + si * ksr[i];
            scratchRe[(size_t) i] = ymr - ysi;                            // Y = Ymid + i * Yside
            scratchIm[(size_t) i] = -(ymi + ysr);                         // conjugate: a forward FFT then acts as the inverse
        }
        fft.forward (scratchRe.data(), scratchIm.data());
        const float inv = 1.0f / (float) kFftN;
        for (int i = 0; i < kHop; ++i)
        {
            dstM[i] = scratchRe[(size_t) (kFftN - kHop + i)] * inv;
            dstS[i] = -scratchIm[(size_t) (kFftN - kHop + i)] * inv;
        }
    }

    bm::Fft fft;
    KernelPair cur, prev;
    bool haveKernels = false, fading = false;
    std::vector<float> inBuf[2], acc[2], outBlock[2];
    std::vector<float> scratchRe, scratchIm, xRe, xIm, oldBlock, oldBlock2;
    int accCount = 0;
};

// ---------------------------------------------------------------------------------------------------------
// The tonal EQ stage. Live mode: minimum-phase IIR filters, zero latency. Broadcast mode: linear-phase FIR,
// latency kEqLatency samples (a disabled EQ is a pure delay of the same length so the chain stays aligned).
// Both modes work on mid/side, so each band can target stereo, mid or side.
class TonalEq
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        reset();
        chains = EqChains::build (params, fs);
        applyChains();
    }

    void reset() noexcept
    {
        for (auto& c : midSt) c.reset();
        for (auto& c : sideSt) c.reset();
        conv.reset();
    }

    void setLinearPhase (bool linear)
    {
        if (linear != linearPhase) { linearPhase = linear; reset(); }
    }

    bool isLinearPhase() const noexcept { return linearPhase; }
    int latency() const noexcept { return linearPhase ? kEqLatency : 0; }
    const EqParams& getParams() const noexcept { return params; }

    // Returns true if the parameters changed (the linear-phase path then needs a new kernel from design()).
    bool setParams (const EqParams& p)
    {
        if (p == params) return false;
        params = p;
        chains = EqChains::build (params, fs);
        applyChains();
        return true;
    }

    // Computes the kernels for the current parameters (call from a worker thread or, in tests, directly).
    void design (KernelPair& out) const { designKernels (params, fs, out, conv.fftEngine()); }
    void applyKernels (const KernelPair& k) { conv.setKernels (k); }
    void applyKernelsNow (const KernelPair& k) { conv.setKernels (k, true); }

    void process (float* l, float* r, int n) noexcept
    {
        if (! linearPhase && chains.nMid == 0 && chains.nSide == 0)
            return;

        for (int i = 0; i < n; ++i)
        {
            const float m = (l[i] + r[i]) * 0.5f, s = (l[i] - r[i]) * 0.5f;
            l[i] = m;
            r[i] = s;
        }

        if (linearPhase)
            conv.process (l, r, n);
        else
        {
            for (int i = 0; i < n; ++i)
            {
                double m = l[i], s = r[i];
                for (int k = 0; k < chains.nMid; ++k) m = midSt[k].process (m);
                for (int k = 0; k < chains.nSide; ++k) s = sideSt[k].process (s);
                l[i] = (float) m;
                r[i] = (float) s;
            }
        }

        for (int i = 0; i < n; ++i)
        {
            const float m = l[i], s = r[i];
            l[i] = m + s;
            r[i] = m - s;
        }
    }

    // Combined response of the mid and side chains, for drawing.
    static double responseDb (const EqParams& p, int channel /*0 mid, 1 side*/, double f, double fs)
    {
        const EqChains c = EqChains::build (p, fs);
        return channel == 0 ? chainResponseDb (c.mid, c.nMid, f, fs) : chainResponseDb (c.side, c.nSide, f, fs);
    }

private:
    void applyChains() noexcept
    {
        for (int i = 0; i < chains.nMid; ++i) chains.mid[i].applyTo (midSt[i]);
        for (int i = 0; i < chains.nSide; ++i) chains.side[i].applyTo (sideSt[i]);
    }

    double fs = 48000.0;
    bool linearPhase = false;
    EqParams params;
    EqChains chains;
    bm::Biquad midSt[EqChains::kMax], sideSt[EqChains::kMax];
    TwoChannelConvolver conv;
};
} // namespace bm::dsp
