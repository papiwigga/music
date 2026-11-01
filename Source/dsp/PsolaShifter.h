#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace afina
{

/** Streaming TD-PSOLA pitch shifter (duration preserving, so formants stay in place).

    Analysis marks are placed one period apart and aligned by cross-correlation with the
    previous period, so grains are always pitch-synchronous. Synthesis grains are
    overlap-added at a spacing of period / ratio and normalised by the sum of windows.
    The output is delayed by exactly latency() samples. */
class PsolaShifter
{
public:
    void prepare (double sampleRate, float minHz, float maxHz)
    {
        sr = sampleRate;
        minPeriod = std::max (16, (int) std::floor (sr / maxHz));
        maxPeriod = (int) std::ceil (sr / minHz);
        defaultPeriod = std::clamp ((int) std::lround (sr / 150.0), minPeriod, maxPeriod);
        maxGrainHalf = (int) std::ceil (maxPeriod * 1.3) + 2;
        latencySamples = 2 * maxPeriod + maxGrainHalf + 8;

        int size = 1;
        while (size < latencySamples + 6 * maxPeriod + 64)
            size <<= 1;
        mask = (int64_t) size - 1;
        inBuf.assign ((size_t) size, 0.0f);
        outBuf.assign ((size_t) size, 0.0f);
        normBuf.assign ((size_t) size, 0.0f);
        reset();
    }

    int latency() const { return latencySamples; }

    void reset()
    {
        std::fill (inBuf.begin(), inBuf.end(), 0.0f);
        std::fill (outBuf.begin(), outBuf.end(), 0.0f);
        std::fill (normBuf.begin(), normBuf.end(), 0.0f);
        writePos = 0;
        anchored = false;
        markHead = markTail = 0;
        lastMarkPos = maxPeriod;
        pushMark (lastMarkPos, defaultPeriod, 1.0f);
        nextSynth = lastMarkPos;
    }

    /** x: input sample. periodSamples: current period estimate (0 if unvoiced).
        ratio: pitch factor to apply (target / current). */
    float process (float x, float periodSamples, float ratio)
    {
        inBuf[(size_t) (writePos & mask)] = x;
        ++writePos;

        const bool voiced = periodSamples > 0.0f;
        int T = voiced ? (int) std::lround (periodSamples) : defaultPeriod;
        T = std::clamp (T, minPeriod, maxPeriod);
        const float r = std::clamp (ratio, 0.5f, 2.0f);

        // 1) Analysis: create marks as soon as enough input is available.
        for (;;)
        {
            const int64_t cand = lastMarkPos + T;
            if (writePos < cand + (3 * (int64_t) T) / 2 + 2)
                break;
            createMark (cand, T, r, voiced);
        }

        // 2) Synthesis: place every grain that can still touch the sample we emit now.
        const int64_t t = writePos - 1 - latencySamples;
        while (nextSynth <= t + maxGrainHalf && lastMarkPos >= nextSynth)
            placeGrain();

        // 3) Emit.
        float y = 0.0f;
        if (t >= 0)
        {
            const size_t idx = (size_t) (t & mask);
            y = outBuf[idx] / std::max (normBuf[idx], 0.25f);
            outBuf[idx] = 0.0f;
            normBuf[idx] = 0.0f;
        }
        return y;
    }

private:
    struct Mark
    {
        int64_t pos = 0;
        int period = 0;
        float ratio = 1.0f;
    };

    static constexpr int markCap = 512;

    float in (int64_t p) const { return p < 0 ? 0.0f : inBuf[(size_t) (p & mask)]; }

    void pushMark (int64_t pos, int period, float ratio)
    {
        if (markHead - markTail >= markCap)
            ++markTail;
        marks[(size_t) (markHead & (markCap - 1))] = { pos, period, ratio };
        ++markHead;
    }

    const Mark& markAt (int64_t i) const { return marks[(size_t) (i & (markCap - 1))]; }

    /** Places the next analysis mark close to 'cand' (= previous mark + one period).
        When a voiced section starts, the mark is anchored to the strongest waveform peak
        (a pulse-like feature, which is where PSOLA grains should be centred). From then on,
        marks follow the waveform by cross-correlation with the previous period and are
        nudged onto the nearest local peak, so they cannot slowly drift away from it. */
    void createMark (int64_t cand, int T, float ratio, bool voiced)
    {
        int bestD = 0;

        if (voiced && ! anchored)
        {
            float bestV = -1e30f;
            for (int d = -T / 2; d <= T / 2; ++d)
            {
                const float v = in (cand + d);
                if (v > bestV)
                {
                    bestV = v;
                    bestD = d;
                }
            }
            anchored = true;
        }
        else
        {
            const int h = T / 2;
            const int dMax = T / 4;

            double refE = 0.0;
            for (int i = -h; i < h; ++i)
            {
                const double v = in (lastMarkPos + i);
                refE += v * v;
            }

            double bestScore = -1e30;
            for (int d = -dMax; d <= dMax; ++d)
            {
                double c = 0.0, e = 0.0;
                for (int i = -h; i < h; ++i)
                {
                    const double a = in (cand + d + i);
                    c += a * in (lastMarkPos + i);
                    e += a * a;
                }
                const double score = c / std::sqrt (e * refE + 1e-12);
                if (score > bestScore)
                {
                    bestScore = score;
                    bestD = d;
                }
            }

            if (voiced)
            {
                const int centre = bestD;
                const int snap = std::max (1, T / 16);
                float bestV = -1e30f;
                for (int d = centre - snap; d <= centre + snap; ++d)
                {
                    const float v = in (cand + d);
                    if (v > bestV)
                    {
                        bestV = v;
                        bestD = d;
                    }
                }
            }
            else
            {
                anchored = false;   // re-anchor on the next voiced section
            }
        }

        const int64_t pos = cand + bestD;
        pushMark (pos, (int) (pos - lastMarkPos), ratio);
        lastMarkPos = pos;
    }

    void placeGrain()
    {
        const int64_t ts = nextSynth;

        // Marks that are already behind ts (with a successor also behind) are obsolete.
        while (markHead - markTail >= 2 && markAt (markTail + 1).pos <= ts)
            ++markTail;

        Mark pick = markAt (markTail);
        if (markHead - markTail >= 2)
        {
            const Mark& b = markAt (markTail + 1);
            if (std::llabs (b.pos - ts) < std::llabs (pick.pos - ts))
                pick = b;
        }

        const float Ts = (float) pick.period / pick.ratio;
        // Shifting up: narrower window so only two grains overlap at any point.
        const int P = std::min ({ pick.period, maxGrainHalf, std::max (minPeriod, (int) std::lround (Ts * 1.15f)) });
        const float invP = 3.14159265358979f / (float) P;
        for (int i = -P; i < P; ++i)
        {
            const float w = 0.5f * (1.0f + std::cos (invP * (float) i));
            const size_t idx = (size_t) ((ts + i) & mask);
            outBuf[idx] += w * in (pick.pos + i);
            normBuf[idx] += w;
        }

        nextSynth += std::max<int64_t> (1, std::llround (Ts));
    }

    double sr = 44100.0;
    int minPeriod = 44, maxPeriod = 551, defaultPeriod = 294, maxGrainHalf = 720, latencySamples = 0;
    int64_t mask = 0, writePos = 0, lastMarkPos = 0, nextSynth = 0;
    int64_t markHead = 0, markTail = 0;
    bool anchored = false;
    std::array<Mark, markCap> marks {};
    std::vector<float> inBuf, outBuf, normBuf;
};

} // namespace afina
