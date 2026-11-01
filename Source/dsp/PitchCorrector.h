#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace afina
{

/** Decides, once per analysis hop, how far the voice has to be moved.
    Takes the raw f0 from the detector, cleans it (median filter, short-gap bridging),
    picks the nearest allowed note (with hysteresis) and smooths the correction
    according to the retune speed. */
class PitchCorrector
{
public:
    struct Params
    {
        std::array<bool, 12> notes {};   // allowed pitch classes, index 0 = C
        float retuneMs = 20.0f;          // 0 = instant (hard tune)
        float humanize = 0.0f;           // 0..1, slows the correction on held notes
        float amount = 1.0f;             // 0..1, how much of the correction is applied
        float refA = 440.0f;
    };

    struct Result
    {
        float ratio = 1.0f;     // pitch shift factor to apply
        float period = 0.0f;    // detected period in samples (0 = unvoiced)
        float inMidi = 0.0f;    // detected pitch (fractional MIDI note), 0 if unvoiced
        float outMidi = 0.0f;   // corrected pitch
        bool voiced = false;
    };

    void prepare (double sampleRate, int hopSamples)
    {
        sr = sampleRate;
        hopDt = (float) hopSamples / (float) sampleRate;
        reset();
    }

    void reset()
    {
        histCount = 0;
        histPos = 0;
        unvoicedRun = 0;
        heldNote = -1;
        holdTime = 0.0f;
        corr = 0.0f;
    }

    Result update (float f0, float clarity, const Params& p)
    {
        Result r;
        const bool detected = f0 > 0.0f && clarity >= 0.7f;

        if (detected)
        {
            unvoicedRun = 0;
            hist[(size_t) histPos] = 69.0f + 12.0f * std::log2 (f0 / p.refA);
            histPos = (histPos + 1) % histSize;
            histCount = std::min (histCount + 1, histSize);
        }
        else if (++unvoicedRun > 3)
        {
            histCount = 0;
        }

        if (histCount == 0)
        {
            heldNote = -1;
            holdTime = 0.0f;
            corr = 0.0f;
            return r;
        }

        // Median of the most recent detections.
        std::array<float, histSize> tmp {};
        for (int k = 0; k < histCount; ++k)
            tmp[(size_t) k] = hist[(size_t) ((histPos - 1 - k + 2 * histSize) % histSize)];
        std::sort (tmp.begin(), tmp.begin() + histCount);
        const float m = tmp[(size_t) (histCount / 2)];

        // Target note with hysteresis.
        int cand = nearestAllowed (m, p.notes);
        if (cand >= 0 && heldNote >= 0 && cand != heldNote && isAllowed (heldNote, p.notes))
            if (std::fabs (m - (float) heldNote) <= std::fabs (m - (float) cand) + 0.15f)
                cand = heldNote;

        if (cand != heldNote)
        {
            heldNote = cand;
            holdTime = 0.0f;
        }
        else
        {
            holdTime += hopDt;
        }

        const float want = cand >= 0 ? ((float) cand - m) * std::clamp (p.amount, 0.0f, 1.0f) : 0.0f;
        const float tau = p.retuneMs * 0.001f
                        + p.humanize * 0.15f * std::min (1.0f, holdTime / 0.5f);
        const float alpha = tau <= 1e-4f ? 1.0f : 1.0f - std::exp (-hopDt / tau);
        corr += (want - corr) * alpha;

        const float f0s = p.refA * std::exp2 ((m - 69.0f) / 12.0f);
        r.ratio = std::clamp (std::exp2 (corr / 12.0f), 0.5f, 2.0f);
        r.period = (float) sr / f0s;
        r.inMidi = m;
        r.outMidi = m + corr;
        r.voiced = true;
        return r;
    }

private:
    static constexpr int histSize = 5;

    static bool isAllowed (int midiNote, const std::array<bool, 12>& notes)
    {
        return notes[(size_t) (((midiNote % 12) + 12) % 12)];
    }

    static int nearestAllowed (float m, const std::array<bool, 12>& notes)
    {
        const int base = (int) std::floor (m + 0.5f);
        int best = -1;
        float bestDist = 1e9f;
        for (int n = base - 7; n <= base + 7; ++n)
        {
            if (! isAllowed (n, notes))
                continue;
            const float d = std::fabs (m - (float) n);
            if (d < bestDist)
            {
                bestDist = d;
                best = n;
            }
        }
        return best;
    }

    double sr = 44100.0;
    float hopDt = 0.004f;
    std::array<float, histSize> hist {};
    int histCount = 0, histPos = 0, unvoicedRun = 0;
    int heldNote = -1;
    float holdTime = 0.0f;
    float corr = 0.0f;   // current correction in semitones
};

} // namespace afina
