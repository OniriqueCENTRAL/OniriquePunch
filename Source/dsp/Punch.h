// Onirique Punch — the multiband transient shaper, without JUCE so the test can drive it alone.
#pragma once

#include <array>
#include <atomic>

namespace punch
{

constexpr int kMaxChannels = 2, kMaxBands = 3;

/** How many points of history the display can read, and how long each one lasts. */
constexpr int kHistory = 2048;
constexpr double kPointsPerSecond = 400.0;

enum Mode { sharp = 0, medium = 1, smooth = 2 };

struct Band
{
    float attack = 0.0f, sustain = 0.0f;   // -100 .. +100 %
    int mode = medium;
    bool on = true, solo = false;
};

struct Settings
{
    int bands = 3;                          // 1, 2 or 3
    float xover1 = 150.0f, xover2 = 2500.0f; // Hz; two bands use the first
    std::array<Band, kMaxBands> band {};

    float inGain = 0.0f, outGain = 0.0f;    // dB
    float mix = 100.0f;                     // %
    float pan = 0.0f, width = 0.0f;         // -100 .. +100
    bool mono = false, phase = false, swap = false, delta = false, linked = true;
};

/** A second-order Butterworth section; two in a row make a Linkwitz-Riley crossover. */
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    std::array<double, kMaxChannels> z1 {}, z2 {};

    void set (double hz, double sampleRate, bool highPass) noexcept;
    float run (int c, float x) noexcept
    {
        const double y = b0 * x + z1[c];
        z1[c] = b1 * x - a1 * y + z2[c];
        z2[c] = b2 * x - a2 * y;
        return (float) y;
    }
    void reset() noexcept { z1 = {}; z2 = {}; }
};

/** One band's envelopes: what tells an attack from a body, and a tail from it. */
struct Detector
{
    float level = 0, fast = 0, slow = 0, body = 0, tail = 0, gainDb = 0;
    void reset() noexcept { *this = {}; }
};

class Shaper
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void setSettings (const Settings& s) noexcept { settings = s; }

    /** In place; up to kMaxChannels channels. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    /** Peaks since last asked, linear: input after its gain, and output, per channel. */
    float takePeak (bool output, int channel) noexcept { return peaks[output ? 1 : 0][channel].exchange (0.0f); }

    /** The display's history, per band: the band's level and the gain applied, one point every 1/kPointsPerSecond s. */
    struct Point { std::atomic<float> level { 0 }, gainDb { 0 }; };
    std::array<std::array<Point, kHistory>, kMaxBands> history;
    std::atomic<int> written { 0 };   // points written so far; the newest is (written - 1) % kHistory

private:
    float shape (Detector&, float level, const Band&) noexcept;
    void updateFilters() noexcept;

    Settings settings;
    double sr = 48000.0;
    float shownX1 = -1, shownX2 = -1;
    int shownBands = -1;

    // low = LR(f1) low pass, then all-passed at f2 (its own LR low + high pass); rest = LR(f1) high pass split at f2.
    Biquad lp1[2], hp1[2], lp2[2], hp2[2], apLp[2], apHp[2];

    // envelope coefficients: fast/slow attack followers, body/tail followers, gain smoothing per mode
    float cLevel = 0, cFast = 0, cRel = 0, cBodyA = 0, cBodyR = 0, cTailR = 0, cParam = 0;
    std::array<float, 3> cSlow {}, cSmooth {};
    std::array<std::array<Detector, kMaxChannels>, kMaxBands> detectors;

    float inLin = 1, outLin = 1, mixLin = 1;   // smoothed
    int pointCount = 0;
    std::array<float, kMaxBands> pointLevel {}, pointGain {};
    std::array<std::array<std::atomic<float>, kMaxChannels>, 2> peaks {};
};

} // namespace punch
