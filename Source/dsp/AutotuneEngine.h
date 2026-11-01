#pragma once

#include "PitchCorrector.h"
#include "PsolaShifter.h"
#include "YinDetector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace afina
{

/** Fixed delay used to keep the dry signal aligned with the processed one. */
class DelayLine
{
public:
    void prepare (int delaySamples)
    {
        size = std::max (1, delaySamples);
        buf.assign ((size_t) size, 0.0f);
        pos = 0;
    }

    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        pos = 0;
    }

    float process (float x)
    {
        const float y = buf[(size_t) pos];
        buf[(size_t) pos] = x;
        if (++pos >= size)
            pos = 0;
        return y;
    }

private:
    std::vector<float> buf;
    int size = 1, pos = 0;
};

/** Complete pitch-correction chain: detection -> correction logic -> PSOLA shifting -> dry/wet.
    Independent of JUCE so it can be tested on its own. */
class AutotuneEngine
{
public:
    static constexpr int maxChannels = 2;
    static constexpr float maxHz = 1000.0f;

    struct Frame
    {
        float inMidi = 0.0f;    // 0 = unvoiced
        float outMidi = 0.0f;
    };

    void prepare (double sampleRate, int channels, float lowestHz = 90.0f)
    {
        minHz = lowestHz;
        sr = sampleRate;
        numCh = std::clamp (channels, 1, maxChannels);

        // Detect on a decimated copy of the signal (~16-22 kHz) to keep the CPU load low.
        decim = std::max (1, (int) std::floor (sr / 16000.0));
        hop = decim * std::max (1, (int) std::ceil (sr * 0.004 / decim));
        detector.prepare (sr / decim, minHz, maxHz);
        corrector.prepare (sr, hop);

        for (int c = 0; c < numCh; ++c)
            shifter[(size_t) c].prepare (sr, minHz, maxHz);

        latencySamples = shifter[0].latency();
        for (int c = 0; c < numCh; ++c)
            delay[(size_t) c].prepare (latencySamples);

        mixCoeff = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));
        voicedCoeff = 1.0f - std::exp (-1.0f / (0.006f * (float) sr));   // ~6 ms crossfade
        reset();
    }

    int latency() const { return latencySamples; }

    void reset()
    {
        detector.reset();
        corrector.reset();
        for (int c = 0; c < numCh; ++c)
        {
            shifter[(size_t) c].reset();
            delay[(size_t) c].reset();
        }
        decimAcc = 0.0f;
        decimCount = 0;
        hopCount = 0;
        state = {};
        mixCur = mixTarget;
        voicedCur = 0.0f;
    }

    void setParams (const PitchCorrector::Params& p, float mix)
    {
        params = p;
        mixTarget = std::clamp (mix, 0.0f, 1.0f);
    }

    /** 'in' and 'out' may point to the same buffers. 'sink' is called with a Frame once per hop. */
    template <typename FrameSink>
    void process (const float* const* in, float* const* out, int numSamples, FrameSink&& sink)
    {
        const float invCh = 1.0f / (float) numCh;
        const float invDecim = 1.0f / (float) decim;

        for (int n = 0; n < numSamples; ++n)
        {
            float mono = 0.0f;
            for (int c = 0; c < numCh; ++c)
                mono += in[c][n];
            mono *= invCh;

            decimAcc += mono;
            if (++decimCount >= decim)
            {
                detector.push (decimAcc * invDecim);
                decimAcc = 0.0f;
                decimCount = 0;
            }

            if (++hopCount >= hop)
            {
                hopCount = 0;
                float clarity = 0.0f;
                const float f0 = detector.detect (clarity);
                state = corrector.update (f0, clarity, params);
                sink (Frame { state.voiced ? state.inMidi : 0.0f, state.voiced ? state.outMidi : 0.0f });
            }

            mixCur += (mixTarget - mixCur) * mixCoeff;

            // Consonants, breaths and silence are passed through untouched (delayed dry signal);
            // only clearly pitched sound goes through the shifter. Smooth crossfade between both.
            voicedCur += ((state.voiced ? 1.0f : 0.0f) - voicedCur) * voicedCoeff;
            const float wetAmount = mixCur * voicedCur;

            for (int c = 0; c < numCh; ++c)
            {
                const float x = in[c][n];
                const float wet = shifter[(size_t) c].process (x, state.period, state.ratio);
                const float dry = delay[(size_t) c].process (x);
                out[c][n] = dry + (wet - dry) * wetAmount;
            }
        }
    }

private:
    double sr = 44100.0;
    float minHz = 90.0f;
    int numCh = 1, decim = 1, hop = 176, latencySamples = 0;
    int decimCount = 0, hopCount = 0;
    float decimAcc = 0.0f, mixCur = 1.0f, mixTarget = 1.0f, mixCoeff = 0.001f;
    float voicedCur = 0.0f, voicedCoeff = 0.01f;

    YinDetector detector;
    PitchCorrector corrector;
    PitchCorrector::Params params;
    PitchCorrector::Result state;
    std::array<PsolaShifter, maxChannels> shifter;
    std::array<DelayLine, maxChannels> delay;
};

} // namespace afina
