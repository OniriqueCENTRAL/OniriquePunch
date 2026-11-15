// Renders the editor to a PNG without a host, and runs the plugin as a host would.
// A development tool, not part of the product.
//
//   punch_snapshot out.png [scale] [browser|menu]
//   punch_snapshot --selftest
#include "../Source/PluginEditor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
constexpr double kRate = 48000.0;

/** A drum loop: a kick and a snare, ringing. */
void drums (juce::AudioProcessor& processor, int blocks)
{
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    static int t = 0;
    static unsigned noise = 7;

    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < 512; ++i, ++t)
        {
            const int beat = t % 24000, half = (t + 12000) % 24000;
            noise = noise * 1664525u + 1013904223u;
            const double nz = (noise >> 9) / 8388608.0 - 0.5;
            const double kick = std::exp (-beat / 4800.0) * std::sin (2.0 * juce::MathConstants<double>::pi * (55.0 + 120.0 * std::exp (-beat / 900.0)) * beat / kRate);
            const double snare = (t / 12000) % 2 == 1 ? std::exp (-half / 3000.0) * (0.5 * nz + 0.3 * std::sin (0.04 * half)) : 0.0;
            const auto v = (float) (0.6 * kick + 0.5 * snare);
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
        }
        processor.processBlock (buffer, midi);
    }
}

double through (juce::AudioProcessor& processor, double hz, bool& finite)
{
    juce::AudioBuffer<float> buffer (2, 700);
    juce::MidiBuffer midi;
    double re = 0, im = 0;
    int n = 0;

    for (int b = 0, t = 0; b < 100; ++b, t += 700)
    {
        for (int i = 0; i < 700; ++i)
            for (int c = 0; c < 2; ++c)
                buffer.setSample (c, i, 0.1f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * (t + i) / kRate));

        processor.processBlock (buffer, midi);

        for (int i = 0; i < 700; ++i)
        {
            const auto v = buffer.getSample (0, i);
            finite = finite && std::isfinite (v);
            if (b >= 60)
            {
                const auto phase = 2.0 * juce::MathConstants<double>::pi * hz * (t + i) / kRate;
                re += v * std::cos (phase);
                im += v * std::sin (phase);
                ++n;
            }
        }
    }

    return 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / n / 0.1);
}

int selfTest (onirique::PunchProcessor& processor, juce::AudioProcessorValueTreeState& state)
{
    int failures = 0;
    auto check = [&] (bool ok, const char* what)
    {
        std::printf ("  [%s] %s\n", ok ? " ok " : "FAIL", what);
        failures += ok ? 0 : 1;
    };
    auto set = [&] (const juce::String& id, float v)
    {
        auto* p = state.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (v));
    };

    bool finite = true;
    check (processor.getLatencySamples() == 0, "no delay to make up for");

    for (double hz : { 60.0, 1000.0, 9000.0 })
    {
        const auto g = through (processor, hz, finite);
        std::printf ("  %.0f Hz untouched: %+.3f dB\n", hz, g);
        check (std::abs (g) < 0.05, "untouched, it comes out as it went in");
    }

    set ("outgain", -6.0f);
    const auto lower = through (processor, 1000.0, finite);
    std::printf ("  output -6 dB: %+.2f dB\n", lower);
    check (std::abs (lower + 6.0) < 0.1, "the output fader is heard");
    set ("outgain", 0.0f);

    set ("bypass", 1.0f);
    set ("ingain", 12.0f);
    check (std::abs (through (processor, 1000.0, finite)) < 0.01, "bypassed: as it came in");
    set ("bypass", 0.0f);
    set ("ingain", 0.0f);
    check (finite, "nothing blows up");

    auto& presets = processor.presets;
    const auto problems = presets.checkStock();
    for (const auto& problem : problems)
        std::printf ("  %s\n", problem.toRawUTF8());
    check (problems.isEmpty(), "the stock presets name real parameters, within their ranges");

    presets.load ("Kick");
    check (presets.getCurrentName() == "Kick" && std::abs (state.getRawParameterValue ("b2attack")->load() - 55.0f) < 0.01f, "a stock preset loads");
    set ("b1solo", 1.0f);
    presets.load ("Room Killer");
    check (state.getRawParameterValue ("b1solo")->load() > 0.5f, "solo is left as it is");

    return failures == 0 ? 0 : 1;
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "snapshot.png");
    const auto scale = argc > 2 ? juce::String (argv[2]).getFloatValue() : 1.0f;

    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    processor->setPlayConfigDetails (2, 2, kRate, 512);
    processor->prepareToPlay (kRate, 512);

    auto& punch = static_cast<onirique::PunchProcessor&> (*processor);

    if (juce::String (argc > 1 ? argv[1] : "") == "--selftest")
        return selfTest (punch, punch.apvts);

    // The kick preset on a drum loop, the high band shown: something to look at.
    punch.presets.load ("Kick");
    punch.apvts.state.setProperty ("selected", 2, nullptr);
    drums (*processor, 200);

    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (punch.createEditor());
        editor->setSize (onirique::PunchEditor::kUnits, onirique::PunchEditor::kTotalHeight);

        const juce::String panel (argc > 3 ? argv[3] : "");

        for (auto* child : editor->getChildren())
        {
            if (dynamic_cast<onirique::license::Overlay*> (child) != nullptr)
                child->setVisible (false);

            if (auto* bar = dynamic_cast<onirique::nuit::Bar*> (child))
            {
                if (panel == "browser") bar->openBrowser();
                else if (panel == "menu") bar->openMenu();
            }
        }

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
        file.deleteFile();
        juce::FileOutputStream stream (file);

        if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, stream))
            return 1;
    }

    std::printf ("%s\n", file.getFullPathName().toRawUTF8());
    return 0;
}
