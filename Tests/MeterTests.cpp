// Standalone checks for the metering engine (no JUCE needed).
// Build:  g++ -std=c++17 -O2 -I../Source/Meters MeterTests.cpp -o meter_tests && ./meter_tests
#include <cstdio>
#include <cmath>
#include <random>
#include <vector>
#include "MeterBank.h"

using namespace bm;

static int failures = 0;

static void check (const char* name, double value, double expected, double tol)
{
    const bool ok = std::fabs (value - expected) <= tol;
    std::printf ("%-52s %9.3f  (expect %8.3f +/- %.2f)  %s\n", name, value, expected, tol, ok ? "ok" : "FAIL");
    if (! ok) ++failures;
}

static const double kFs = 48000.0;

// Feeds `seconds` of a stereo signal through the bank in 512-sample blocks.
template <typename Gen>
static void run (MeterBank& bank, double seconds, Gen gen)
{
    std::vector<float> l (512), r (512);
    static long long t = 0;
    const long long total = (long long) (seconds * kFs);
    for (long long done = 0; done < total; done += 512)
    {
        const int n = (int) std::min<long long> (512, total - done);
        for (int i = 0; i < n; ++i) { gen (t++, l[(size_t) i], r[(size_t) i]); }
        bank.process (l.data(), r.data(), n);
    }
}

static double dbToLin (double db) { return std::pow (10.0, db / 20.0); }

int main()
{
    // 1. 997 Hz stereo sine at -23 dBFS peak must read -23.0 LUFS everywhere, with no loudness range.
    {
        MeterBank b; b.prepare (kFs);
        const double a = dbToLin (-23.0);
        run (b, 30.0, [&] (long long t, float& l, float& r) { l = r = (float) (a * std::sin (2.0 * kPi * 997.0 * t / kFs)); });
        const auto s = b.snapshot();
        check ("997 Hz -23 dBFS stereo: momentary LUFS", s.momentary, -23.0, 0.1);
        check ("997 Hz -23 dBFS stereo: short-term LUFS", s.shortTerm, -23.0, 0.1);
        check ("997 Hz -23 dBFS stereo: integrated LUFS", s.integrated, -23.0, 0.1);
        check ("997 Hz -23 dBFS stereo: LRA", s.lra, 0.0, 0.2);
        check ("997 Hz -23 dBFS stereo: sample peak dB", s.samplePeak, -23.0, 0.1);
        check ("997 Hz -23 dBFS stereo: correlation", s.correlation, 1.0, 0.01);
        check ("997 Hz -23 dBFS stereo: mono loss dB", s.monoLossDb, 0.0, 0.1);
        check ("997 Hz -23 dBFS stereo: balance dB", s.balanceDb, 0.0, 0.1);
        check ("997 Hz -23 dBFS stereo: crest dB (sine = 3.01)", s.crest, 3.01, 0.1);
        // a mono signal's mid level equals its channel level (-26.0 dB RMS for a -23 dB peak sine), side is silent
        check ("mono: mid RMS dB equals channel RMS", s.midRms, -26.0, 0.3);
        check ("mono: side RMS dB is silent", s.sideRms <= -100.0 ? 1.0 : 0.0, 1.0, 0.01);
    }

    // 1b. Left only: balance reads LEFT (negative), correlation 0, mid and side both 6 dB under the channel level.
    {
        MeterBank b; b.prepare (kFs);
        const double a = dbToLin (-20.0);
        run (b, 10.0, [&] (long long t, float& l, float& r) { l = (float) (a * std::sin (2.0 * kPi * 997.0 * t / kFs)); r = 0.0f; });
        const auto s = b.snapshot();
        check ("left only: balance is negative (left louder)", s.balanceDb < -20.0 ? 1.0 : 0.0, 1.0, 0.01);
        check ("left only: mid RMS", s.midRms, -29.0, 0.3);
        check ("left only: side RMS", s.sideRms, -29.0, 0.3);
    }

    // 2. Relative gate: 20 s at -23, then 20 s at -60. Integrated must ignore the quiet section.
    {
        MeterBank b; b.prepare (kFs);
        const double loud = dbToLin (-23.0), quiet = dbToLin (-60.0);
        run (b, 20.0, [&] (long long t, float& l, float& r) { l = r = (float) (loud  * std::sin (2.0 * kPi * 997.0 * t / kFs)); });
        run (b, 20.0, [&] (long long t, float& l, float& r) { l = r = (float) (quiet * std::sin (2.0 * kPi * 997.0 * t / kFs)); });
        check ("Relative gate: integrated LUFS", b.snapshot().integrated, -23.0, 0.1);
    }

    // 3. Absolute gate: below -70 LUFS is never counted.
    {
        MeterBank b; b.prepare (kFs);
        const double loud = dbToLin (-30.0), floorSig = dbToLin (-80.0);
        run (b, 10.0, [&] (long long t, float& l, float& r) { l = r = (float) (loud     * std::sin (2.0 * kPi * 997.0 * t / kFs)); });
        run (b, 30.0, [&] (long long t, float& l, float& r) { l = r = (float) (floorSig * std::sin (2.0 * kPi * 997.0 * t / kFs)); });
        check ("Absolute gate: integrated LUFS", b.snapshot().integrated, -30.0, 0.1);
    }

    // 4. Loudness range: 25 s at -23 then 25 s at -33 should give about 10 LU.
    {
        MeterBank b; b.prepare (kFs);
        const double a1 = dbToLin (-23.0), a2 = dbToLin (-33.0);
        run (b, 25.0, [&] (long long t, float& l, float& r) { l = r = (float) (a1 * std::sin (2.0 * kPi * 997.0 * t / kFs)); });
        run (b, 25.0, [&] (long long t, float& l, float& r) { l = r = (float) (a2 * std::sin (2.0 * kPi * 997.0 * t / kFs)); });
        check ("Loudness range: LRA", b.snapshot().lra, 10.0, 0.3);
    }

    // 5. K-weighting relative to the 997 Hz reference (which itself sits +0.69 dB above flat).
    //    Analytically: the 38.1 Hz, Q=0.5 high pass is -1.18 dB at 100 Hz, so 100 Hz reads about -1.85 dB
    //    relative to 997 Hz; the +4 dB shelf is fully open at 10 kHz, giving about +3.3 dB relative.
    {
        MeterBank b100, b10k; b100.prepare (kFs); b10k.prepare (kFs);
        const double a = dbToLin (-20.0);
        run (b100, 10.0, [&] (long long t, float& l, float& r) { l = r = (float) (a * std::sin (2.0 * kPi * 100.0   * t / kFs)); });
        run (b10k, 10.0, [&] (long long t, float& l, float& r) { l = r = (float) (a * std::sin (2.0 * kPi * 10000.0 * t / kFs)); });
        check ("K-weighting at 100 Hz relative to 997 Hz", b100.snapshot().momentary + 20.0, -1.85, 0.15);
        check ("K-weighting at 10 kHz relative to 997 Hz", b10k.snapshot().momentary + 20.0, 3.3, 0.2);
    }

    // 6. True peak: a fs/4 sine sampled at 45 degrees has sample peaks of -3.01 dB but a true peak of 0 dB.
    {
        MeterBank b; b.prepare (kFs);
        run (b, 2.0, [&] (long long t, float& l, float& r) { l = r = (float) std::sin (2.0 * kPi * 0.25 * t + kPi / 4.0); });
        const auto s = b.snapshot();
        check ("True peak: sample peak dB", s.samplePeak, -3.01, 0.05);
        check ("True peak: true peak dBTP", s.tpHold, 0.0, 0.15);
    }

    // 7. Stereo: out of phase, independent noise, left only.
    {
        MeterBank b; b.prepare (kFs);
        const double a = dbToLin (-20.0);
        run (b, 3.0, [&] (long long t, float& l, float& r) { l = (float) (a * std::sin (2.0 * kPi * 440.0 * t / kFs)); r = -l; });
        auto s = b.snapshot();
        check ("Out of phase: correlation", s.correlation, -1.0, 0.01);
        check ("Out of phase: mono loss dB (large)", s.monoLossDb, -60.0, 30.0);

        MeterBank n; n.prepare (kFs);
        std::mt19937 rng (1234); std::normal_distribution<float> dist (0.0f, 0.1f);
        run (n, 10.0, [&] (long long, float& l, float& r) { l = dist (rng); r = dist (rng); });
        s = n.snapshot();
        check ("Independent noise: correlation", s.correlation, 0.0, 0.1);
        check ("Independent noise: mono loss dB (about -3)", s.monoLossDb, -3.0, 0.5);

        MeterBank lo; lo.prepare (kFs);
        run (lo, 3.0, [&] (long long t, float& l, float& r) { l = (float) (a * std::sin (2.0 * kPi * 440.0 * t / kFs)); r = 0.0f; });
        s = lo.snapshot();
        check ("Left only: balance dB (strongly left, negative)", s.balanceDb, -24.0, 0.5);
    }

    // 8. Spectrum: 1 kHz at -6 dBFS reads -6 dB at its bin, and the 1/6 octave band around 1 kHz is close.
    {
        MeterBank b; b.prepare (kFs);
        const double a = dbToLin (-6.0);
        run (b, 1.0, [&] (long long t, float& l, float& r) { l = r = (float) (a * std::sin (2.0 * kPi * 1000.0 * t / kFs)); });
        SpectrumAnalyzer sa; sa.prepare (kFs);
        check ("Spectrum: 1 kHz sine at -6 dBFS, peak bin dB", sa.peakBinDb (b.ringL), -6.0, 1.5); // Hann scalloping loss up to 1.4 dB
        std::vector<float> bands;
        sa.analyse (b.ringL, b.ringR, Weighting::Flat, bands);
        int best = 0;
        for (int i = 1; i < (int) bands.size(); ++i) if (bands[(size_t) i] > bands[(size_t) best]) best = i;
        check ("Spectrum: loudest 1/6-oct band centre Hz", sa.bandCentre (best), 1000.0, 90.0);
    }

    // 9. Weighting curves at 1 kHz are 0 dB; A at 100 Hz is about -19.1 dB; C at 100 Hz is about -0.3 dB.
    check ("A-weighting at 1 kHz", weightingDb (Weighting::A, 1000.0), 0.0, 0.05);
    check ("A-weighting at 100 Hz", weightingDb (Weighting::A, 100.0), -19.1, 0.2);
    check ("C-weighting at 1 kHz", weightingDb (Weighting::C, 1000.0), 0.0, 0.05);
    check ("C-weighting at 100 Hz", weightingDb (Weighting::C, 100.0), -0.3, 0.1);

    // 10. Reset clears integrated loudness.
    {
        MeterBank b; b.prepare (kFs);
        const double a = dbToLin (-20.0);
        run (b, 5.0, [&] (long long t, float& l, float& r) { l = r = (float) (a * std::sin (2.0 * kPi * 997.0 * t / kFs)); });
        b.requestReset();
        run (b, 0.2, [&] (long long, float& l, float& r) { l = r = 0.0f; });
        check ("Reset: integrated LUFS back to floor", b.snapshot().integrated, -120.0, 0.1);
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
