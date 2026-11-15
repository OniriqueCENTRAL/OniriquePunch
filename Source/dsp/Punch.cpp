#include "Punch.h"

#include <algorithm>
#include <cmath>

namespace punch
{

namespace
{
    float coefficient (double ms, double sampleRate) noexcept
    {
        return (float) std::exp (-1.0 / (ms * 0.001 * sampleRate));
    }

    void follow (float& env, float x, float attack, float release) noexcept
    {
        const float c = x > env ? attack : release;
        env = c * env + (1.0f - c) * x;
    }

    float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

    float runTwice (Biquad* pair, int c, float x) noexcept { return pair[1].run (c, pair[0].run (c, x)); }
}

void Biquad::set (double hz, double sampleRate, bool highPass) noexcept
{
    const double w0 = 2.0 * M_PI * std::min (hz, sampleRate * 0.45) / sampleRate;
    const double cosw = std::cos (w0), alpha = std::sin (w0) / (2.0 * M_SQRT1_2);
    const double a0 = 1.0 + alpha;

    if (highPass) { b0 = (1.0 + cosw) * 0.5; b1 = -(1.0 + cosw); }
    else          { b0 = (1.0 - cosw) * 0.5; b1 = 1.0 - cosw; }

    b2 = b0;
    b0 /= a0; b1 /= a0; b2 /= a0;
    a1 = -2.0 * cosw / a0;
    a2 = (1.0 - alpha) / a0;
}

void Shaper::prepare (double sampleRate) noexcept
{
    sr = sampleRate;
    cLevel = coefficient (8.0, sr);
    cFast = coefficient (0.1, sr);
    cRel = coefficient (60.0, sr);
    cBodyA = coefficient (1.0, sr);
    cBodyR = coefficient (25.0, sr);
    cTailR = coefficient (350.0, sr);
    cParam = 1.0f - coefficient (20.0, sr);

    // Sharp catches only the very start of a hit; Smooth lets the boost spread over the first tens of ms.
    const double slow[] = { 4.0, 12.0, 30.0 }, smoothing[] = { 0.15, 0.6, 2.5 };

    for (int m = 0; m < 3; ++m)
    {
        cSlow[(size_t) m] = coefficient (slow[m], sr);
        cSmooth[(size_t) m] = 1.0f - coefficient (smoothing[m], sr);
    }

    shownBands = -1;
    reset();
}

void Shaper::reset() noexcept
{
    for (auto* set : { lp1, hp1, lp2, hp2, apLp, apHp })
        for (int k = 0; k < 2; ++k)
            set[k].reset();

    for (auto& band : detectors)
        for (auto& d : band)
            d.reset();

    inLin = dbToGain (settings.inGain);
    outLin = dbToGain (settings.outGain);
    mixLin = settings.mix * 0.01f;
    pointCount = 0;
    pointLevel = {};
    pointGain = {};
}

void Shaper::updateFilters() noexcept
{
    const float f1 = std::clamp (settings.xover1, 20.0f, 18000.0f);
    const float f2 = std::clamp (std::max (settings.xover2, f1 * 1.25f), 25.0f, 20000.0f);

    if (f1 == shownX1 && f2 == shownX2 && settings.bands == shownBands)
        return;

    shownX1 = f1; shownX2 = f2; shownBands = settings.bands;

    for (int k = 0; k < 2; ++k)
    {
        lp1[k].set (f1, sr, false); hp1[k].set (f1, sr, true);
        lp2[k].set (f2, sr, false); hp2[k].set (f2, sr, true);
        apLp[k].set (f2, sr, false); apHp[k].set (f2, sr, true);
    }
}

float Shaper::shape (Detector& d, float level, const Band& b) noexcept
{
    const auto mode = (size_t) std::clamp (b.mode, 0, 2);

    // The followers listen to a peak level held over a few ms, so a steady tone's ripple reads as steady.
    follow (d.level, level, 0.0f, cLevel);
    follow (d.fast, d.level, cFast, cRel);
    follow (d.slow, d.level, cSlow[mode], cRel);
    follow (d.body, d.level, cBodyA, cBodyR);
    follow (d.tail, d.level, cBodyA, cTailR);

    float target = 0.0f;

    // Silence is left alone: no hiss lifted between the hits.
    if (d.fast > 1.0e-5f)
    {
        constexpr float eps = 1.0e-7f;
        const float onset = 20.0f * std::log10 ((d.fast + eps) / (d.slow + eps));   // > 0 while a hit starts
        const float decay = 20.0f * std::log10 ((d.tail + eps) / (d.body + eps));   // > 0 while it rings out
        target = b.attack * 0.01f * 0.9f * onset + b.sustain * 0.01f * 0.8f * decay;
    }

    target = std::clamp (target, -30.0f, 18.0f);
    d.gainDb += (target - d.gainDb) * cSmooth[mode];
    return d.gainDb;
}

void Shaper::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    numChannels = std::clamp (numChannels, 0, kMaxChannels);
    if (numChannels == 0)
        return;

    updateFilters();

    const auto& s = settings;
    const int bands = std::clamp (s.bands, 1, kMaxBands);
    const float inTarget = dbToGain (s.inGain), outTarget = dbToGain (s.outGain), mixTarget = std::clamp (s.mix, 0.0f, 100.0f) * 0.01f;
    const int samplesPerPoint = std::max (1, (int) (sr / kPointsPerSecond));

    bool anySolo = false;
    for (int b = 0; b < bands; ++b)
        anySolo = anySolo || s.band[(size_t) b].solo;

    std::array<float, kMaxChannels> inPeak {}, outPeak {};

    for (int i = 0; i < numSamples; ++i)
    {
        inLin += (inTarget - inLin) * cParam;
        outLin += (outTarget - outLin) * cParam;
        mixLin += (mixTarget - mixLin) * cParam;

        float split[kMaxBands][kMaxChannels] = {};

        for (int c = 0; c < numChannels; ++c)
        {
            const float x = channels[c][i] * inLin;
            inPeak[(size_t) c] = std::max (inPeak[(size_t) c], std::fabs (x));

            if (bands == 1)
                split[0][c] = x;
            else if (bands == 2)
            {
                split[0][c] = runTwice (lp1, c, x);
                split[1][c] = runTwice (hp1, c, x);
            }
            else
            {
                const float low = runTwice (lp1, c, x), rest = runTwice (hp1, c, x);
                split[0][c] = runTwice (apLp, c, low) + runTwice (apHp, c, low);
                split[1][c] = runTwice (lp2, c, rest);
                split[2][c] = runTwice (hp2, c, rest);
            }
        }

        float gains[kMaxBands][kMaxChannels] = {};

        for (int b = 0; b < bands; ++b)
        {
            const auto& band = s.band[(size_t) b];
            float level = 0.0f;

            for (int c = 0; c < numChannels; ++c)
                level = std::max (level, std::fabs (split[b][c]));

            if (s.linked)
            {
                const float g = shape (detectors[(size_t) b][0], level, band);
                for (int c = 0; c < numChannels; ++c)
                    gains[b][c] = g;
            }
            else
                for (int c = 0; c < numChannels; ++c)
                    gains[b][c] = shape (detectors[(size_t) b][(size_t) c], std::fabs (split[b][c]), band);

            // The display keeps the loudest moment and the strongest gain of each point.
            pointLevel[(size_t) b] = std::max (pointLevel[(size_t) b], level);
            if (std::fabs (gains[b][0]) > std::fabs (pointGain[(size_t) b]))
                pointGain[(size_t) b] = gains[b][0];
        }

        for (int c = 0; c < numChannels; ++c)
        {
            float dry = 0.0f, wet = 0.0f;

            for (int b = 0; b < bands; ++b)
            {
                const auto& band = s.band[(size_t) b];
                if (anySolo && ! band.solo)
                    continue;

                dry += split[b][c];
                wet += band.on ? split[b][c] * dbToGain (gains[b][c]) : split[b][c];
            }

            channels[c][i] = (s.delta ? wet - dry : dry + (wet - dry) * mixLin) * outLin;
        }

        if (numChannels == 2)
        {
            float l = channels[0][i], r = channels[1][i];

            if (s.swap) std::swap (l, r);
            if (s.mono) l = r = 0.5f * (l + r);

            const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * (1.0f + std::clamp (s.width, -100.0f, 100.0f) * 0.01f);
            l = mid + side; r = mid - side;

            const float pan = std::clamp (s.pan, -100.0f, 100.0f) * 0.01f;
            l *= std::min (1.0f, 1.0f - pan);
            r *= std::min (1.0f, 1.0f + pan);

            channels[0][i] = l; channels[1][i] = r;
        }

        for (int c = 0; c < numChannels; ++c)
        {
            if (s.phase)
                channels[c][i] = -channels[c][i];
            outPeak[(size_t) c] = std::max (outPeak[(size_t) c], std::fabs (channels[c][i]));
        }

        if (++pointCount >= samplesPerPoint)
        {
            const int at = written.load (std::memory_order_relaxed) % kHistory;

            for (int b = 0; b < kMaxBands; ++b)
            {
                history[(size_t) b][(size_t) at].level.store (pointLevel[(size_t) b], std::memory_order_relaxed);
                history[(size_t) b][(size_t) at].gainDb.store (pointGain[(size_t) b], std::memory_order_relaxed);
            }

            written.fetch_add (1, std::memory_order_release);
            pointCount = 0;
            pointLevel = {};
            pointGain = {};
        }
    }

    for (int c = 0; c < numChannels; ++c)
        for (int k = 0; k < 2; ++k)
        {
            auto& peak = peaks[(size_t) k][(size_t) c];
            const float now = k == 0 ? inPeak[(size_t) c] : outPeak[(size_t) c];
            if (now > peak.load (std::memory_order_relaxed))
                peak.store (now, std::memory_order_relaxed);
        }
}

} // namespace punch
