#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace onirique
{

namespace
{
    using Attributes = AudioParameterFloatAttributes;

    float parseNumber (const String& text)
    {
        auto t = text.trim().toLowerCase();
        const bool kilo = t.endsWith ("k") || t.endsWith ("khz");
        const auto v = t.retainCharacters ("0123456789.,-").replaceCharacter (',', '.').getFloatValue();
        return kilo ? v * 1000.0f : v;
    }

    Attributes reading (std::function<String (float)> show)
    {
        return Attributes().withStringFromValueFunction ([show] (float v, int) { return show (v); })
                           .withValueFromStringFunction ([] (const String& t) { return parseNumber (t); });
    }

    String signedPercent (float v) { return (v > 0.5f ? "+" : "") + String (roundToInt (v)) + " %"; }
    String decibels (float v)      { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; }
    String hertz (float v)         { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (roundToInt (v)) + " Hz"; }

    NormalisableRange<float> frequencies (float start, float end)
    {
        return { start, end, [] (float a, float b, float p) { return a * std::pow (b / a, p); },
                             [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
                             [] (float a, float b, float v) { return jlimit (a, b, v); } };
    }

    AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        AudioProcessorValueTreeState::ParameterLayout p;

        auto number = [&] (const String& id, const String& name, NormalisableRange<float> range, float value, Attributes a)
        { p.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, value, a)); };
        auto toggle = [&] (const String& id, const String& name, bool value)
        { p.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, value)); };

        p.add (std::make_unique<AudioParameterChoice> (ParameterID { "bands", 1 }, "Bands", StringArray { "1", "2", "3" }, 2));
        number ("xover1", "Crossover Low", frequencies (40.0f, 4000.0f), 150.0f, reading (hertz));
        number ("xover2", "Crossover High", frequencies (400.0f, 16000.0f), 2500.0f, reading (hertz));

        const char* names[] = { "Low", "Mid", "High" };

        for (int b = 0; b < punch::kMaxBands; ++b)
        {
            const String name (names[b]);
            number (PunchProcessor::bandId (b, "attack"), name + " Attack", { -100.0f, 100.0f, 0.1f }, 0.0f, reading (signedPercent));
            number (PunchProcessor::bandId (b, "sustain"), name + " Sustain", { -100.0f, 100.0f, 0.1f }, 0.0f, reading (signedPercent));
            p.add (std::make_unique<AudioParameterChoice> (ParameterID { PunchProcessor::bandId (b, "mode"), 1 }, name + " Mode",
                                                           StringArray { "Sharp", "Medium", "Smooth" }, 1));
            toggle (PunchProcessor::bandId (b, "on"), name + " On", true);
            toggle (PunchProcessor::bandId (b, "solo"), name + " Solo", false);
        }

        number ("ingain", "Input Gain", { -24.0f, 12.0f, 0.1f }, 0.0f, reading (decibels));
        number ("outgain", "Output Gain", { -24.0f, 12.0f, 0.1f }, 0.0f, reading (decibels));
        number ("mix", "Mix", { 0.0f, 100.0f, 0.1f }, 100.0f, reading ([] (float v) { return String (roundToInt (v)) + " %"; }));
        number ("pan", "Pan", { -100.0f, 100.0f, 1.0f }, 0.0f,
                reading ([] (float v) { return std::abs (v) < 0.5f ? String ("C") : String (roundToInt (std::abs (v))) + (v < 0 ? "L" : "R"); }));
        number ("width", "Width", { -100.0f, 100.0f, 1.0f }, 0.0f, reading ([] (float v) { return String (roundToInt (v)); }));
        toggle ("mono", "Mono", false);
        toggle ("phase", "Phase Invert", false);
        toggle ("swap", "Swap L/R", false);
        toggle ("delta", "Delta", false);
        toggle ("linked", "Stereo Link", true);
        toggle ("bypass", "Bypass", false);

        return p;
    }

    enum Tags { drums = 1, bass = 2, vocal = 4, instruments = 8, mix = 16 };

    std::vector<PresetBank::Stock> stockPresets()
    {
        auto band = [] (int b, float attack, float sustain, float mode)
        {
            return std::vector<std::pair<String, float>> { { PunchProcessor::bandId (b, "attack"), attack },
                                                           { PunchProcessor::bandId (b, "sustain"), sustain },
                                                           { PunchProcessor::bandId (b, "mode"), mode } };
        };
        auto join = [] (std::initializer_list<std::vector<std::pair<String, float>>> parts, std::vector<std::pair<String, float>> extra = {})
        {
            std::vector<std::pair<String, float>> all;
            for (auto& part : parts) all.insert (all.end(), part.begin(), part.end());
            all.insert (all.end(), extra.begin(), extra.end());
            return all;
        };

        return {
            { "Default", 0, {} },
            { "Kick", drums, join ({ band (0, 35, -10, 1), band (1, 55, -20, 0), band (2, 20, 0, 1) }) },
            { "Snare Crack", drums, join ({ band (0, 10, 0, 1), band (1, 60, -15, 0), band (2, 45, 10, 0) }, { { "xover1", 200.0f }, { "xover2", 3500.0f } }) },
            { "Drum Bus Snap", drums | mix, join ({ band (0, 25, -10, 1), band (1, 35, -10, 1), band (2, 25, 0, 1) }, { { "mix", 70.0f } }) },
            { "Room Killer", drums, join ({ band (0, 10, -50, 1), band (1, 15, -60, 1), band (2, 10, -45, 2) }) },
            { "Room Booster", drums, join ({ band (0, -10, 45, 2), band (1, -15, 60, 2), band (2, -5, 40, 2) }) },
            { "Bass Tightener", bass, join ({ band (0, 20, -35, 1), band (1, 40, -25, 0) }, { { "bands", 1.0f }, { "xover1", 250.0f } }) },
            { "Pick Attack", instruments, join ({ band (0, 0, -10, 1), band (1, 30, -10, 1), band (2, 55, 0, 0) }) },
            { "Soft Vocal", vocal, join ({ band (0, -20, 0, 2), band (1, -35, 10, 2), band (2, -45, 0, 2) }) },
            { "Mix Punch", mix, join ({ band (0, 20, 0, 1), band (1, 25, -5, 1), band (2, 15, 0, 2) }, { { "mix", 60.0f } }) },
        };
    }
}

//==============================================================================
PunchProcessor::PunchProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Punch", createLayout()),
      // Listening aids, not settings: a preset leaves them as they are.
      presets (apvts, "Punch", stockPresets(), { "Drums", "Bass", "Vocal", "Instruments", "Mix" },
               { "bypass", "delta", "b1solo", "b2solo", "b3solo" })
{
    bypassParameter = apvts.getParameter ("bypass");
}

bool PunchProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return (out == AudioChannelSet::mono() || out == AudioChannelSet::stereo()) && layouts.getMainInputChannelSet() == out;
}

punch::Settings PunchProcessor::readSettings() const noexcept
{
    auto value = [this] (const String& id) { return apvts.getRawParameterValue (id)->load(); };
    auto on = [&] (const String& id) { return value (id) > 0.5f; };

    punch::Settings s;
    s.bands = roundToInt (value ("bands")) + 1;
    s.xover1 = value ("xover1");
    s.xover2 = value ("xover2");

    for (int b = 0; b < punch::kMaxBands; ++b)
    {
        auto& band = s.band[(size_t) b];
        band.attack = value (bandId (b, "attack"));
        band.sustain = value (bandId (b, "sustain"));
        band.mode = roundToInt (value (bandId (b, "mode")));
        band.on = on (bandId (b, "on"));
        band.solo = on (bandId (b, "solo"));
    }

    s.inGain = value ("ingain");
    s.outGain = value ("outgain");
    s.mix = value ("mix");
    s.pan = value ("pan");
    s.width = value ("width");
    s.mono = on ("mono");
    s.phase = on ("phase");
    s.swap = on ("swap");
    s.delta = on ("delta");
    s.linked = on ("linked");
    return s;
}

void PunchProcessor::prepareToPlay (double sampleRate, int)
{
    license.refresh();
    shaper.setSettings (readSettings());
    shaper.prepare (sampleRate);
}

void PunchProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;

    if (apvts.getRawParameterValue ("bypass")->load() < 0.5f)
    {
        shaper.setSettings (readSettings());
        shaper.process (buffer.getArrayOfWritePointers(), jmin (buffer.getNumChannels(), punch::kMaxChannels), buffer.getNumSamples());
    }

    // Last, and over every output: what an unlicensed plugin lets through.
    license.process (buffer, getSampleRate(), isNonRealtime());
}

AudioProcessorEditor* PunchProcessor::createEditor()
{
    return new PunchEditor (*this);
}

void PunchProcessor::getStateInformation (MemoryBlock& destination)
{
    auto state = apvts.copyState();
    presets.writeState (state);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destination);
}

void PunchProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes); xml != nullptr && xml->hasTagName (apvts.state.getType()))
    {
        auto state = ValueTree::fromXml (*xml);
        presets.readState (state);
        apvts.replaceState (state);
    }
}

} // namespace onirique

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new onirique::PunchProcessor();
}
