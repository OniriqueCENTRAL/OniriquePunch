// Onirique Punch — editor
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "License/LicenseOverlay.h"
#include "PluginProcessor.h"
#include "ui/OniriqueBar.h"
#include "ui/OniriqueDA.h"

namespace onirique
{

/** The transient shaper's window: the bands and their crossovers on top, the
    gain each band applies scrolling under them with its controls floating
    over it, the meters and the output on the right. Laid out in points on a
    1100-point front and zoomed as one with the window. */
class PunchEditor final : public AudioProcessorEditor,
                          private Timer
{
public:
    explicit PunchEditor (PunchProcessor&);
    ~PunchEditor() override;

    void paint (Graphics&) override;
    void resized() override;

    static constexpr int kUnits = 1100, kBarHeight = 50, kFrontHeight = 590, kTotalHeight = kBarHeight + kFrontHeight;

    PunchProcessor& processor;
    license::theme::Fonts fonts;

    /** The band whose controls are shown, within the bands in use. */
    int selected() const;
    void select (int band);

    /** What the meters show, in dB: in L/R, out L/R, and their held peaks. */
    float level[4] { -100, -100, -100, -100 }, held[4] { -100, -100, -100, -100 };

private:
    void timerCallback() override;

    struct Look;
    struct Front final : Component
    {
        std::function<void (Graphics&)> onPaint;
        void paint (Graphics& g) override { onPaint (g); }
    };

    std::unique_ptr<Look> look;
    Front front;
    nuit::Bar bar;
    std::unique_ptr<Component> module, strip, graph, side;
    std::vector<float> seenValues;
    int seenSelected = -1, themeSeen = -1;
    Image backdrop;
    int heldFor[4] {};

    TooltipWindow tooltips { this, 1500 };
    std::unique_ptr<license::Overlay> licenseOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PunchEditor)
};

} // namespace onirique
