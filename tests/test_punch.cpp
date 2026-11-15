// The shaper alone: flat when untouched, louder hits with attack, shorter tails with sustain, nothing blows up.
#include "dsp/Punch.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double kRate = 48000.0;
int failures = 0;

void check (bool ok, const char* what)
{
    std::printf ("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    failures += ok ? 0 : 1;
}

/** A tone through the shaper: its level coming out, in dB against going in. */
double toneGain (punch::Settings s, double hz)
{
    punch::Shaper shaper;
    shaper.setSettings (s);
    shaper.prepare (kRate);

    const int n = 48000;
    std::vector<float> l (n), r (n);
    for (int i = 0; i < n; ++i)
        l[(size_t) i] = r[(size_t) i] = 0.25f * (float) std::sin (2.0 * M_PI * hz * i / kRate);

    float* ch[] = { l.data(), r.data() };
    shaper.process (ch, 2, n);

    double re = 0, im = 0;
    for (int i = n / 2; i < n; ++i)
    {
        re += l[(size_t) i] * std::cos (2.0 * M_PI * hz * i / kRate);
        im += l[(size_t) i] * std::sin (2.0 * M_PI * hz * i / kRate);
    }
    return 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / (n / 2) / 0.25);
}

/** A drum-like hit every 250 ms; returns the output's peak and its energy 60-200 ms after each hit. */
void hits (punch::Settings s, double& peak, double& tail, bool& finite)
{
    punch::Shaper shaper;
    shaper.setSettings (s);
    shaper.prepare (kRate);

    const int period = 12000, n = period * 8;
    std::vector<float> l (n), r (n);
    unsigned noise = 1;
    for (int i = 0; i < n; ++i)
    {
        const int t = i % period;
        noise = noise * 1664525u + 1013904223u;
        const double nz = ((noise >> 9) / 8388608.0 - 0.5);
        const double v = std::exp (-t / (0.08 * kRate)) * (0.6 * std::sin (2.0 * M_PI * 90.0 * t / kRate) + 0.4 * nz);
        l[(size_t) i] = r[(size_t) i] = (float) (0.5 * v);
    }

    float* ch[] = { l.data(), r.data() };
    for (int at = 0; at < n; at += 512)
    {
        float* part[] = { ch[0] + at, ch[1] + at };
        shaper.process (part, 2, std::min (512, n - at));
    }

    peak = 0; tail = 0;
    for (int i = period * 4; i < n; ++i)
    {
        finite = finite && std::isfinite (l[(size_t) i]);
        peak = std::max (peak, (double) std::fabs (l[(size_t) i]));
        const int t = i % period;
        if (t > 2880 && t < 9600)
            tail += (double) l[(size_t) i] * l[(size_t) i];
    }
}
}

int main()
{
    punch::Settings flat;

    for (double hz : { 50.0, 150.0, 1000.0, 2500.0, 12000.0 })
    {
        const double g = toneGain (flat, hz);
        std::printf ("  untouched, 3 bands, %.0f Hz: %+.3f dB\n", hz, g);
        check (std::fabs (g) < 0.05, "the bands add back up flat");
    }

    {
        punch::Settings s;
        for (auto& b : s.band) { b.attack = 100; b.sustain = -100; }
        const double g = toneGain (s, 1000.0);
        std::printf ("  steady tone, attack +100 / sustain -100: %+.3f dB\n", g);
        check (std::fabs (g) < 0.3, "a steady tone has no transient to shape");
    }

    bool finite = true;
    double peak0, tail0, peakUp, tailUp, peakDown, tailDown, peakShort, tailShort;
    hits (flat, peak0, tail0, finite);

    punch::Settings up;
    for (auto& b : up.band) b.attack = 100;
    hits (up, peakUp, tailUp, finite);

    punch::Settings down;
    for (auto& b : down.band) b.attack = -100;
    hits (down, peakDown, tailDown, finite);

    punch::Settings shorter;
    for (auto& b : shorter.band) b.sustain = -100;
    hits (shorter, peakShort, tailShort, finite);

    std::printf ("  peaks: flat %.3f, attack +100 %.3f, attack -100 %.3f\n", peak0, peakUp, peakDown);
    std::printf ("  tails: flat %.4f, sustain -100 %.4f\n", tail0, tailShort);
    check (peakUp > peak0 * 1.3, "attack + makes the hits stand out");
    check (peakDown < peak0 * 0.8, "attack - softens them");
    check (tailShort < tail0 * 0.5, "sustain - shortens the tails");

    punch::Settings wild;
    wild.bands = 2;
    wild.inGain = 12; wild.outGain = 12; wild.width = 100; wild.pan = -100; wild.delta = true; wild.linked = false;
    for (auto& b : wild.band) { b.attack = 100; b.sustain = 100; b.mode = punch::sharp; }
    double p, t;
    hits (wild, p, t, finite);
    check (finite, "nothing blows up at the extremes");

    std::printf (failures == 0 ? "all good\n" : "%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
