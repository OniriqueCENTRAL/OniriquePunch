// Onirique Punch — plugin entry point
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "License/LicenseGuard.h"
#include "dsp/Punch.h"
#include "ui/OniriquePresets.h"

namespace onirique
{

using namespace juce;

/** A multiband transient shaper: up to three bands, each with its attack and sustain. */
class PunchProcessor final : public AudioProcessor
{
public:
    PunchProcessor();
    ~PunchProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override { shaper.reset(); }
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (AudioBuffer<float>&, MidiBuffer&) override;

    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override     { return false; }
    bool producesMidi() const override    { return false; }
    bool isMidiEffect() const override    { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    AudioProcessorParameter* getBypassParameter() const override { return bypassParameter; }

    int getNumPrograms() override            { return 1; }
    int getCurrentProgram() override         { return 0; }
    void setCurrentProgram (int) override    {}
    const String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const String&) override {}

    void getStateInformation (MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** The parameter id of band b's control: "b1attack", "b2mode"… */
    static String bandId (int band, const char* what) { return "b" + String (band + 1) + what; }

    AudioProcessorValueTreeState apvts;
    PresetBank presets;
    onirique::license::Guard license { "onirique-punch" };

    /** What the display and meters read; written by the audio thread. */
    punch::Shaper shaper;

private:
    punch::Settings readSettings() const noexcept;

    AudioProcessorParameter* bypassParameter = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PunchProcessor)
};

} // namespace onirique
