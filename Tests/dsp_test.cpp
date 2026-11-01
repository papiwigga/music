// Stand-alone tests for the DSP engine (no JUCE needed).
// Build:  g++ -O2 -std=c++17 -I../Source/dsp dsp_test.cpp -o dsp_test && ./dsp_test

#include "AutotuneEngine.h"

#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <random>

using namespace afina;

static const double kSr = 44100.0;
static int failures = 0;

static void check (bool ok, const char* name, const char* detail)
{
    std::printf ("[%s] %-46s %s\n", ok ? " OK " : "FAIL", name, detail);
    if (! ok)
        ++failures;
}

static float cents (float f, float ref) { return 1200.0f * std::log2 (f / ref); }

// Vocal-like signal: glottal pulse train -> spectral tilt -> three fixed formants.
// Fixed formants are what a real vowel looks like, and what PSOLA is designed to preserve.
struct Resonator
{
    float a1 = 0, a2 = 0, b0 = 0, y1 = 0, y2 = 0;
    void set (float freq, float bw)
    {
        const float r = std::exp (-(float) M_PI * bw / (float) kSr);
        a1 = -2.0f * r * std::cos (2.0f * (float) M_PI * freq / (float) kSr);
        a2 = r * r;
        b0 = 1.0f - r;
    }
    float process (float x)
    {
        const float y = b0 * x - a1 * y1 - a2 * y2;
        y2 = y1;
        y1 = y;
        return y;
    }
};

template <typename FreqFn>
static std::vector<float> makeVoice (int n, FreqFn freqAt)
{
    std::vector<float> x ((size_t) n);
    Resonator f1, f2, f3;
    f1.set (700.0f, 100.0f);
    f2.set (1220.0f, 120.0f);
    f3.set (2600.0f, 160.0f);
    double phase = 0.0;
    float tilt = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        phase += (double) freqAt (i) / kSr;
        float pulse = 0.0f;
        if (phase >= 1.0)
        {
            phase -= 1.0;
            pulse = 1.0f;
        }
        tilt += 0.35f * (pulse - tilt);
        x[(size_t) i] = 3.0f * (f1.process (tilt) + 0.6f * f2.process (tilt) + 0.3f * f3.process (tilt));
    }
    return x;
}

static std::vector<float> makeNoise (int n, float amp)
{
    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> d (-amp, amp);
    std::vector<float> x ((size_t) n);
    for (auto& v : x)
        v = d (rng);
    return x;
}

static float rms (const std::vector<float>& x, size_t a, size_t b)
{
    double e = 0.0;
    for (size_t i = a; i < b; ++i)
        e += (double) x[i] * x[i];
    return (float) std::sqrt (e / (double) (b - a));
}

// f0 of the last 'len' samples ending at 'end'.
static float f0At (const std::vector<float>& x, size_t end)
{
    YinDetector det;
    det.prepare (kSr, 90.0f, 1000.0f);
    const size_t len = 1400;
    for (size_t i = end - len; i < end; ++i)
        det.push (x[i]);
    float clarity = 0.0f;
    return det.detect (clarity);
}

static void runEngine (AutotuneEngine& eng, const std::vector<float>& in, std::vector<float>& out,
                       std::vector<AutotuneEngine::Frame>* frames = nullptr)
{
    out.assign (in.size(), 0.0f);
    const int block = 512;
    for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, in.size() - pos);
        const float* inPtr[1] = { in.data() + pos };
        float* outPtr[1] = { out.data() + pos };
        eng.process (inPtr, outPtr, n, [&] (const AutotuneEngine::Frame& f) { if (frames) frames->push_back (f); });
    }
}

static PitchCorrector::Params makeParams (std::initializer_list<int> allowed, float retuneMs)
{
    PitchCorrector::Params p;
    for (int n : allowed)
        p.notes[(size_t) n] = true;
    p.retuneMs = retuneMs;
    return p;
}

int main()
{
    char buf[256];

    // 1) YIN accuracy ---------------------------------------------------------------
    {
        float worst = 0.0f;
        for (float f : { 100.0f, 150.0f, 220.0f, 330.0f, 440.0f, 660.0f, 880.0f })
        {
            auto x = makeVoice (8000, [f] (int) { return f; });
            worst = std::max (worst, std::fabs (cents (f0At (x, 8000), f)));
        }
        std::snprintf (buf, sizeof buf, "worst error %.2f cents", worst);
        check (worst < 5.0f, "YIN: accuracy 100-880 Hz", buf);
    }

    // 2) YIN: silence and noise are unvoiced ---------------------------------------------
    {
        std::vector<float> silence (8000, 0.0f);
        auto noise = makeNoise (8000, 0.3f);
        YinDetector det;
        det.prepare (kSr, 90.0f, 1000.0f);
        float c = 0.0f;
        for (size_t i = 6000; i < 8000; ++i)
            det.push (noise[i]);
        const float fn = det.detect (c);
        const float fs = f0At (silence, 8000);
        std::snprintf (buf, sizeof buf, "silence=%.1f Hz, noise=%.1f Hz (clarity %.2f)", fs, fn, c);
        check (fs == 0.0f && (fn == 0.0f || c < 0.7f), "YIN: silence/noise not voiced", buf);
    }

    // 3) PSOLA identity (ratio 1): same signal as the input delayed by latency(), up to a constant
    //    waveform-phase offset (PSOLA only keeps grains pitch-synchronous, not phase-locked).
    {
        PsolaShifter sh;
        sh.prepare (kSr, 90.0f, 1000.0f);
        const int n = 44100;
        auto x = makeVoice (n, [] (int) { return 220.5f; });   // exactly 200 samples / period
        std::vector<float> y ((size_t) n);
        for (int i = 0; i < n; ++i)
            y[(size_t) i] = sh.process (x[(size_t) i], 200.0f, 1.0f);

        const int L = sh.latency();
        double bestErr = 1e9;
        int bestLag = 0;
        for (int lag = -110; lag <= 110; ++lag)
        {
            double err = 0.0, ref = 0.0;
            for (int i = 25000; i < 40000; ++i)
            {
                const double d = (double) y[(size_t) i] - x[(size_t) (i - L + lag)];
                err += d * d;
                ref += (double) x[(size_t) (i - L + lag)] * x[(size_t) (i - L + lag)];
            }
            if (std::sqrt (err / ref) < bestErr)
            {
                bestErr = std::sqrt (err / ref);
                bestLag = lag;
            }
        }
        std::snprintf (buf, sizeof buf, "latency %d, offset %d samples, relative error %.2f %%", L, bestLag, 100.0 * bestErr);
        check (bestErr < 0.05, "PSOLA: ratio 1 reproduces the input", buf);
    }

    // 4) PSOLA shifts: pitch and level --------------------------------------------------------
    {
        float worstCents = 0.0f, worstGain = 0.0f;
        for (float semis : { -5.0f, -2.0f, -0.5f, 0.5f, 2.0f, 5.0f })
        {
            for (float f : { 130.0f, 220.0f, 330.0f })
            {
                PsolaShifter sh;
                sh.prepare (kSr, 90.0f, 1000.0f);
                const int n = 44100;
                auto x = makeVoice (n, [f] (int) { return f; });
                std::vector<float> y ((size_t) n);
                const float ratio = std::exp2 (semis / 12.0f);
                for (int i = 0; i < n; ++i)
                    y[(size_t) i] = sh.process (x[(size_t) i], (float) (kSr / f), ratio);
                const float fo = f0At (y, 40000);
                worstCents = std::max (worstCents, std::fabs (cents (fo, f * ratio)));
                const float g = rms (y, 20000, 40000) / rms (x, 20000, 40000);
                worstGain = std::max (worstGain, std::fabs (20.0f * std::log10 (g)));
            }
        }
        std::snprintf (buf, sizeof buf, "worst pitch error %.2f cents, level deviation %.2f dB", worstCents, worstGain);
        check (worstCents < 10.0f && worstGain < 2.5f, "PSOLA: pitch & level (-5..+5 semitones)", buf);
    }

    // 5) Engine: hard tune to the nearest note (chromatic) ---------------------------------------
    {
        AutotuneEngine eng;
        eng.prepare (kSr, 1);
        eng.setParams (makeParams ({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, 0.0f), 1.0f);
        eng.reset();
        auto x = makeVoice (88200, [] (int) { return 215.0f; });
        std::vector<float> y;
        runEngine (eng, x, y);
        const float fo = f0At (y, 80000);
        std::snprintf (buf, sizeof buf, "215 Hz -> %.2f Hz (target 220.00, %.1f cents)", fo, cents (fo, 220.0f));
        check (std::fabs (cents (fo, 220.0f)) < 8.0f, "Engine: 215 Hz goes to A3 (chromatic)", buf);
    }

    // 6) Engine: scale respected (C major, A#3 -> A3) --------------------------------------------
    {
        AutotuneEngine eng;
        eng.prepare (kSr, 1);
        eng.setParams (makeParams ({ 0, 2, 4, 5, 7, 9, 11 }, 0.0f), 1.0f);
        eng.reset();
        auto x = makeVoice (88200, [] (int) { return 232.0f; });
        std::vector<float> y;
        runEngine (eng, x, y);
        const float fo = f0At (y, 80000);
        std::snprintf (buf, sizeof buf, "232 Hz -> %.2f Hz (target 220.00)", fo);
        check (std::fabs (cents (fo, 220.0f)) < 8.0f, "Engine: C major scale (A#3 -> A3)", buf);
    }

    // 7) Engine: vibrato is flattened at speed 0 and kept at slow speed -------------------------
    {
        auto vib = [] (int i) { return 220.0f * (1.0f + 0.02f * std::sin (2.0f * (float) M_PI * 5.5f * (float) i / (float) kSr)); };
        auto x = makeVoice (132300, vib);

        auto spread = [&] (float retuneMs, float& meanC)
        {
            AutotuneEngine eng;
            eng.prepare (kSr, 1);
            eng.setParams (makeParams ({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, retuneMs), 1.0f);
            eng.reset();
            std::vector<float> y;
            runEngine (eng, x, y);
            std::vector<float> cs;
            for (size_t e = 60000; e < 130000; e += 1500)
            {
                const float f = f0At (y, e);
                if (f > 0.0f)
                    cs.push_back (cents (f, 220.0f));
            }
            meanC = std::accumulate (cs.begin(), cs.end(), 0.0f) / (float) cs.size();
            double v = 0.0;
            for (float c : cs)
                v += (c - meanC) * (c - meanC);
            return (float) std::sqrt (v / (double) cs.size());
        };

        float m0, m1;
        const float sdHard = spread (0.0f, m0);
        const float sdSoft = spread (400.0f, m1);
        std::snprintf (buf, sizeof buf, "std dev: hard %.1f cents, slow %.1f cents (input ~ +-34)", sdHard, sdSoft);
        check (sdHard < 0.65f * 24.0f && sdSoft > 0.8f * 24.0f, "Engine: retune speed (vibrato flattened / kept)", buf);
    }

    // 8) Engine: noise (unvoiced) passes through at similar level ---------------------------------
    {
        AutotuneEngine eng;
        eng.prepare (kSr, 1);
        eng.setParams (makeParams ({ 0, 2, 4, 5, 7, 9, 11 }, 0.0f), 1.0f);
        eng.reset();
        auto x = makeNoise (88200, 0.2f);
        std::vector<float> y;
        runEngine (eng, x, y);
        const float g = rms (y, 40000, 80000) / rms (x, 40000, 80000);
        std::snprintf (buf, sizeof buf, "gain %.2f", g);
        check (g > 0.7f && g < 1.3f, "Engine: unvoiced signal keeps its level", buf);
    }

    // 9) Engine: mix = 0 returns the exact input delayed by latency() ---------------------------
    {
        AutotuneEngine eng;
        eng.prepare (kSr, 1);
        eng.setParams (makeParams ({ 0, 2, 4, 5, 7, 9, 11 }, 0.0f), 0.0f);
        eng.reset();
        auto x = makeNoise (30000, 0.5f);
        std::vector<float> y;
        runEngine (eng, x, y);
        const int L = eng.latency();
        double maxErr = 0.0;
        for (size_t i = (size_t) L; i < x.size(); ++i)
            maxErr = std::max (maxErr, (double) std::fabs (y[i] - x[i - (size_t) L]));
        std::snprintf (buf, sizeof buf, "latency %d samples, max error %.2g", L, maxErr);
        check (maxErr < 1e-6, "Engine: dry path aligned with latency", buf);
    }

    // 10) Latency per voice range, and the engine still tunes correctly at the smallest one -------
    {
        float lat[3];
        int i = 0;
        for (float lo : { 90.0f, 130.0f, 180.0f })
        {
            AutotuneEngine e;
            e.prepare (kSr, 1, lo);
            lat[i++] = (float) e.latency() * 1000.0f / (float) kSr;
        }
        AutotuneEngine eng;
        eng.prepare (kSr, 1, 180.0f);
        eng.setParams (makeParams ({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, 0.0f), 1.0f);
        eng.reset();
        auto x = makeVoice (88200, [] (int) { return 305.0f; });   // ~D#4 flat -> 311.13 Hz
        std::vector<float> y;
        runEngine (eng, x, y);
        const float fo = f0At (y, 80000);
        std::snprintf (buf, sizeof buf, "latency %.1f / %.1f / %.1f ms; 305 Hz -> %.2f Hz (target 311.13)", lat[0], lat[1], lat[2], fo);
        check (std::fabs (cents (fo, 311.13f)) < 10.0f, "Engine: latency per range, tunes at 180 Hz+", buf);
    }

    std::printf ("\n%s\n", failures == 0 ? "All tests passed." : "THERE ARE FAILURES.");
    return failures == 0 ? 0 : 1;
}
