#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace afina
{

/** Monophonic pitch detector based on YIN (de Cheveigne & Kawahara, 2002).
    No JUCE dependency and no allocations after prepare(). */
class YinDetector
{
public:
    void prepare (double sampleRate, float minHz, float maxHz)
    {
        sr = sampleRate;
        tauMin = std::max (2, (int) std::floor (sr / maxHz));
        tauMax = (int) std::ceil (sr / minHz);
        window = tauMax;

        int size = 1;
        while (size < window + tauMax + 8)
            size <<= 1;

        ring.assign ((size_t) size, 0.0f);
        mask = (int64_t) size - 1;
        linear.assign ((size_t) (window + tauMax), 0.0f);
        cmnd.assign ((size_t) tauMax + 1, 1.0f);
        writePos = 0;
        prevF0 = 0.0f;
        misses = 0;
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        writePos = 0;
        prevF0 = 0.0f;
        misses = 0;
    }

    void push (float x)
    {
        ring[(size_t) (writePos & mask)] = x;
        ++writePos;
    }

    /** Estimates f0 over the most recent samples.
        Returns the frequency in Hz, or 0 if the signal is silent / unvoiced.
        'clarity' is in [0,1]; higher means a more periodic signal. */
    float detect (float& clarity, float margin = 0.1f, float silenceRms = 0.002f)
    {
        clarity = 0.0f;
        const int n = window + tauMax;

        double energy = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float v = ring[(size_t) ((writePos - n + i) & mask)];
            linear[(size_t) i] = v;
            energy += (double) v * v;
        }

        if (std::sqrt (energy / n) < silenceRms)
            return fail();

        // Difference function + cumulative mean normalised difference.
        const float* a = linear.data();
        double running = 0.0;
        cmnd[0] = 1.0f;

        for (int tau = 1; tau <= tauMax; ++tau)
        {
            const float* b = a + tau;
            float sum = 0.0f;
            for (int j = 0; j < window; ++j)
            {
                const float d = a[j] - b[j];
                sum += d * d;
            }
            running += sum;
            cmnd[(size_t) tau] = running > 1e-12 ? (float) (sum * tau / running) : 1.0f;
        }

        // Global minimum: must be reasonably periodic, otherwise the frame is unvoiced.
        float gmin = 1e9f;
        for (int tau = tauMin; tau < tauMax; ++tau)
            gmin = std::min (gmin, cmnd[(size_t) tau]);
        if (gmin > 0.3f)
            return fail();

        // Candidates: every local minimum close to the global one.
        // - Without history (start of a note): take the shortest period among the good ones.
        //   That avoids sub-octave errors when 2T matches as well as T.
        // - With history: pick the candidate with the best (match quality + distance to the
        //   previous pitch). Real voices have cycle-to-cycle jitter that sometimes makes 2T look
        //   better than T; the continuity term stops those octave jumps.
        int best = -1;
        float bestCost = 1e9f;
        for (int tau = std::max (tauMin, 2); tau < tauMax - 1; ++tau)
        {
            const float c = cmnd[(size_t) tau];
            if (! (c <= cmnd[(size_t) tau - 1] && c < cmnd[(size_t) tau + 1]) || c > gmin + 0.25f)
                continue;

            if (prevF0 > 0.0f)
            {
                const float cost = c + 0.3f * std::fabs (std::log2 ((float) (sr / tau) / prevF0));
                if (cost < bestCost)
                {
                    bestCost = cost;
                    best = tau;
                }
            }
            else if (c <= gmin + margin)
            {
                best = tau;
                break;
            }
        }
        if (best < 0)
            return fail();

        // Parabolic interpolation around the minimum.
        float tauF = (float) best;
        if (best > 1 && best < tauMax)
        {
            const float s0 = cmnd[(size_t) best - 1];
            const float s1 = cmnd[(size_t) best];
            const float s2 = cmnd[(size_t) best + 1];
            const float denom = s0 + s2 - 2.0f * s1;
            if (std::fabs (denom) > 1e-9f)
                tauF += 0.5f * (s0 - s2) / denom;
        }

        clarity = std::clamp (1.0f - cmnd[(size_t) best], 0.0f, 1.0f);
        const float f0 = (float) (sr / tauF);
        prevF0 = f0;
        misses = 0;
        return f0;
    }

private:
    /** Unvoiced frame: forget the previous pitch after ~50 ms without a clear detection. */
    float fail()
    {
        if (++misses > 12)
            prevF0 = 0.0f;
        return 0.0f;
    }

    float prevF0 = 0.0f;
    int misses = 0;
    double sr = 44100.0;
    int tauMin = 0, tauMax = 0, window = 0;
    int64_t writePos = 0, mask = 0;
    std::vector<float> ring, linear, cmnd;
};

} // namespace afina
