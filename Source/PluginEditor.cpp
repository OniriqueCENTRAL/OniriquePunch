#include "PluginEditor.h"
#include "ui/OniriqueWindowSize.h"

#include <BinaryData.h>

namespace onirique
{

namespace
{
    const Colour bone { 0xfff4ecec }, haze { 0xffa8939a }, mist { 0xffd9c6c9 }, ink { 0xff0b0708 };

    // Where things lie on the front, in points.
    const Rectangle<float> kLeftCard { 16, 12, 844, 566 }, kSideCard { 876, 12, 208, 566 };
    constexpr float kModuleH = 38, kStripH = 66;

    Colour bandColour (int b) { return b == 0 ? look::ember : b == 1 ? look::emberPink : Colour (0xffffdadd); }

    using State = AudioProcessorValueTreeState;
    float value (State& s, const String& id) { return s.getRawParameterValue (id)->load(); }
    bool isOn (State& s, const String& id) { return value (s, id) > 0.5f; }
    int bandCount (State& s) { return jlimit (1, 3, roundToInt (value (s, "bands")) + 1); }

    void set (State& s, const String& id, float plain)
    {
        if (auto* p = s.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (plain));
            p->endChangeGesture();
        }
    }

    void flip (State& s, const String& id) { set (s, id, isOn (s, id) ? 0.0f : 1.0f); }

    /** The edges of the bands in use, in Hz, from 20 Hz to 20 kHz. */
    std::vector<float> edges (State& s)
    {
        const auto n = bandCount (s);
        const auto x1 = value (s, "xover1"), x2 = jmax (value (s, "xover2"), x1 * 1.25f);
        if (n == 1) return { 20.0f, 20000.0f };
        if (n == 2) return { 20.0f, x1, 20000.0f };
        return { 20.0f, x1, x2, 20000.0f };
    }

    float xOf (float hz, float width) { return width * std::log10 (jlimit (20.0f, 20000.0f, hz) / 20.0f) / 3.0f; }
    float hzOf (float x, float width) { return 20.0f * std::pow (10.0f, 3.0f * x / width); }

    String hzText (float hz)
    {
        if (hz >= 1000.0f)
            return String (hz / 1000.0f, hz < 9950.0f ? 1 : 0).replaceCharacter ('.', ',') + " kHz";
        return String (roundToInt (hz)) + " Hz";
    }

    String dbText (float db, int decimals = 1)
    {
        if (db <= -99.0f) return String::fromUTF8 ("−inf");
        const auto t = String (std::abs (db), decimals).replaceCharacter ('.', ',');
        return (db < -0.05f ? String::fromUTF8 ("−") : db > 0.05f ? String ("+") : String()) + t;
    }

    String bandName (int b, int count)
    {
        if (count == 1) return "FULL";
        if (count == 2) return b == 0 ? "LOW" : "HIGH";
        return b == 0 ? "LOW" : b == 1 ? "MID" : "HIGH";
    }

    void star (Graphics& g, Point<float> c, float r, Colour colour)
    {
        Path p;
        const float k = r * 0.24f;
        p.startNewSubPath (c.x, c.y - r);
        p.lineTo (c.x + k, c.y - k); p.lineTo (c.x + r, c.y); p.lineTo (c.x + k, c.y + k);
        p.lineTo (c.x, c.y + r); p.lineTo (c.x - k, c.y + k); p.lineTo (c.x - r, c.y); p.lineTo (c.x - k, c.y - k);
        p.closeSubPath();
        g.setColour (colour);
        g.fillPath (p);
    }

    /** A well holding segments: dark, rounded, a hairline. */
    void well (Graphics& g, Rectangle<float> r, float corner = 9.0f)
    {
        g.setColour (Colours::black.withAlpha (0.42f));
        g.fillRoundedRectangle (r, corner);
        g.setColour (Colours::white.withAlpha (0.07f));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    }

    void text (Graphics& g, const Font& f, const String& s, Rectangle<float> r, Colour c, Justification j = Justification::centred)
    {
        g.setFont (f);
        g.setColour (c);
        g.drawText (s, r, j, false);
    }

    /** A line drawn with a soft light around it. */
    void glowStroke (Graphics& g, const Path& p, Colour c, float width)
    {
        g.setColour (c.withAlpha (0.08f)); g.strokePath (p, PathStrokeType (width * 5.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (c.withAlpha (0.22f)); g.strokePath (p, PathStrokeType (width * 2.4f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (c.interpolatedWith (Colours::white, 0.2f));
        g.strokePath (p, PathStrokeType (width, PathStrokeType::curved, PathStrokeType::rounded));
    }

    Path modeIcon (int mode, Rectangle<float> r)
    {
        Path p;
        const auto x = r.getX(), y = r.getY(), w = r.getWidth() / 18.0f, h = r.getHeight() / 12.0f;
        p.startNewSubPath (x, y + 11 * h);
        if (mode == 0)      { p.lineTo (x + 3 * w, y + 1 * h); p.quadraticTo (x + 5 * w, y + 9 * h, x + 18 * w, y + 10 * h); }
        else if (mode == 1) { p.lineTo (x + 6 * w, y + 2 * h); p.quadraticTo (x + 9 * w, y + 9 * h, x + 18 * w, y + 10 * h); }
        else                { p.quadraticTo (x + 6 * w, y, x + 10 * w, y + 5 * h); p.quadraticTo (x + 12 * w, y + 9 * h, x + 18 * w, y + 10 * h); }
        return p;
    }

    //==============================================================================
    /** Something to click, drawn by whoever owns it. */
    struct Pad final : Component, SettableTooltipClient
    {
        std::function<void (Graphics&, Rectangle<float>, bool over)> draw;
        std::function<void()> onClick;

        void paint (Graphics& g) override { if (draw) draw (g, getLocalBounds().toFloat(), isMouseOver()); }
        void mouseEnter (const MouseEvent&) override { repaint(); }
        void mouseExit (const MouseEvent&) override { repaint(); }
        void mouseUp (const MouseEvent& e) override
        {
            if (e.mouseWasClicked() && getLocalBounds().contains (e.getPosition()) && onClick)
                onClick();
        }
    };

    /** A segment lit or not, its star when lit, its label. */
    void segment (Graphics& g, Rectangle<float> r, const String& label, bool lit, bool over, const Font& f)
    {
        da::seg (g, r, lit ? 1.0f : 0.0f, over ? 1.0f : 0.0f, 1.0f, 7.0f);
        const auto width = GlyphArrangement::getStringWidth (f, label);
        const auto starW = lit ? 12.0f : 0.0f;
        auto x = r.getCentreX() - (width + starW) * 0.5f;
        if (lit) star (g, { x + 4.0f, r.getCentreY() }, 4.0f, look::emberPink);
        text (g, f, label, { x + starW, r.getY(), width + 2, r.getHeight() }, lit ? bone : mist, Justification::centredLeft);
    }
}

//==============================================================================
struct PunchEditor::Look final : LookAndFeel_V4
{
    license::theme::Fonts fonts;

    int getSliderThumbRadius (Slider& s) override
    {
        const auto kind = s.getProperties()["kind"].toString();
        return kind == "box" ? 0 : kind == "fader" ? 8 : 9;
    }

    void drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle, Slider& s) override
    {
        const auto kind = s.getProperties()["kind"].toString();
        const Rectangle<float> r ((float) x, (float) y, (float) w, (float) h);

        if (kind == "band")
        {
            const Colour colour ((uint32) (int64) s.getProperties()["colour"]);
            const float cx = r.getCentreX(), mid = r.getCentreY();
            g.setColour (Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (cx - 2.0f, r.getY(), 4.0f, r.getHeight(), 2.0f);
            g.setColour (Colours::white.withAlpha (0.3f));
            g.fillRect (cx - 7.0f, mid - 0.5f, 14.0f, 1.0f);

            const auto fill = Rectangle<float> (cx - 2.0f, jmin (mid, pos), 4.0f, std::abs (pos - mid));
            g.setColour (colour.withAlpha (0.25f));
            g.fillRoundedRectangle (fill.expanded (2.0f, 0.0f), 3.0f);
            g.setColour (colour);
            g.fillRoundedRectangle (fill, 2.0f);

            const auto thumb = Rectangle<float> (16.0f, 16.0f).withCentre ({ cx, pos });
            g.setColour (Colours::black.withAlpha (0.5f));
            g.fillEllipse (thumb.translated (0.0f, 2.0f));
            g.setColour (colour.withAlpha (s.isMouseOverOrDragging() ? 0.35f : 0.18f));
            g.fillEllipse (thumb.expanded (5.0f));
            g.setGradientFill (ColourGradient (bone, cx, thumb.getY(), Colour (0xffb9a9ad), cx, thumb.getBottom(), false));
            g.fillEllipse (thumb);
            g.setColour (colour);
            g.drawEllipse (thumb.reduced (1.0f), 2.0f);
        }
        else if (kind == "fader")
        {
            const float cx = r.getCentreX();
            g.setColour (Colours::black.withAlpha (0.6f));
            g.fillRoundedRectangle (cx - 3.0f, r.getY() - 4.0f, 6.0f, r.getHeight() + 8.0f, 3.0f);
            g.setColour (Colours::white.withAlpha (0.06f));
            g.drawRoundedRectangle (cx - 3.0f, r.getY() - 4.0f, 6.0f, r.getHeight() + 8.0f, 3.0f, 1.0f);

            const auto cap = Rectangle<float> (26.0f, 15.0f).withCentre ({ cx, pos });
            g.setColour (Colours::black.withAlpha (0.55f));
            g.fillRoundedRectangle (cap.translated (0.0f, 2.5f), 4.0f);
            g.setGradientFill (ColourGradient (bone, cx, cap.getY(), Colour (0xff8f7f84), cx, cap.getBottom(), false));
            g.fillRoundedRectangle (cap, 4.0f);
            g.setColour (Colours::white.withAlpha (0.7f));
            g.fillRect (cap.getX() + 3.0f, cap.getY() + 1.0f, cap.getWidth() - 6.0f, 1.0f);
            g.setColour (ink.withAlpha (0.8f));
            g.fillRect (cap.getX() + 4.0f, pos - 0.75f, cap.getWidth() - 8.0f, 1.5f);
        }
        else if (kind == "mix")
        {
            const float cy = r.getCentreY();
            g.setColour (Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (r.getX(), cy - 2.5f, r.getWidth(), 5.0f, 2.5f);
            g.setGradientFill (ColourGradient (look::emberDeep, r.getX(), cy, look::ember, pos, cy, false));
            g.fillRoundedRectangle (r.getX(), cy - 2.5f, pos - r.getX(), 5.0f, 2.5f);
            const auto thumb = Rectangle<float> (14.0f, 14.0f).withCentre ({ pos, cy });
            g.setColour (look::ember.withAlpha (0.3f));
            g.fillEllipse (thumb.expanded (4.0f));
            g.setColour (bone);
            g.fillEllipse (thumb);
        }
        else // "box": a value to drag
        {
            well (g, r.reduced (0.5f), 7.0f);
            if (s.isMouseOverOrDragging())
            {
                g.setColour (look::ember.withAlpha (0.5f));
                g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
            }
            text (g, fonts->monoBoldFont (12.0f), s.getTextFromValue (s.getValue()), r, bone);
        }
    }

    Rectangle<int> getTooltipBounds (const String& tip, Point<int> at, Rectangle<int> area) override
    {
        const auto layout = layoutTooltip (tip);
        const auto w = roundToInt (layout.getWidth()) + 22, h = roundToInt (layout.getHeight()) + 14;
        return Rectangle<int> (at.x > area.getCentreX() ? at.x - w - 12 : at.x + 18, at.y > area.getCentreY() ? at.y - h - 10 : at.y + 18, w, h)
                   .constrainedWithin (area);
    }

    void drawTooltip (Graphics& g, const String& tip, int width, int height) override
    {
        const auto r = Rectangle<float> ((float) width, (float) height).reduced (0.5f);
        g.setGradientFill (ColourGradient (Colour (0xff3d0911), 0.0f, 0.0f, Colour (0xff1c0508), 0.0f, (float) height, false));
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (look::ember.withAlpha (0.6f));
        g.drawRoundedRectangle (r, 7.0f, 1.0f);
        layoutTooltip (tip).draw (g, r.reduced (11.0f, 7.0f));
    }

    TextLayout layoutTooltip (const String& tip)
    {
        AttributedString s;
        s.append (tip, fonts->bodyFont (13.0f), bone);
        TextLayout layout;
        layout.createLayout (s, 260.0f);
        return layout;
    }
};

namespace
{
    //==============================================================================
    /** DELTA, the stereo link, and how many bands. */
    struct ModuleRow final : Component
    {
        explicit ModuleRow (PunchEditor& e) : ed (e)
        {
            auto& s = ed.processor.apvts;
            const auto mono = ed.fonts->monoBoldFont (11.0f, 0.06f);

            delta.draw = [&s, mono] (Graphics& g, Rectangle<float> r, bool over) { segment (g, r, "DELTA", isOn (s, "delta"), over, mono); };
            delta.onClick = [&s] { flip (s, "delta"); };
            delta.setTooltip (String::fromUTF8 ("Delta : n'écoute que ce que Punch ajoute ou retire au son. Le plus simple pour régler l'attaque et le sustain à l'oreille."));

            link.draw = [&s, mono] (Graphics& g, Rectangle<float> r, bool over) { segment (g, r, "LINK", isOn (s, "linked"), over, mono); };
            link.onClick = [&s] { flip (s, "linked"); };
            link.setTooltip (String::fromUTF8 ("Link : les deux canaux sont façonnés ensemble, l'image stéréo ne bouge pas. Éteint, chaque canal suit ses propres attaques."));

            for (int i = 0; i < 3; ++i)
            {
                count[i].draw = [&s, mono, i] (Graphics& g, Rectangle<float> r, bool over) { segment (g, r, String (i + 1), bandCount (s) == i + 1, over, mono); };
                count[i].onClick = [&s, i] { set (s, "bands", (float) i); };
                count[i].setTooltip (String::fromUTF8 ("Le nombre de bandes : 1 pour tout le son, 2 ou 3 pour façonner le grave, le médium et l'aigu chacun à leur façon."));
            }

            for (auto* c : { &delta, &link, &count[0], &count[1], &count[2] })
                addAndMakeVisible (c);
        }

        void paint (Graphics& g) override
        {
            well (g, delta.getBounds().toFloat().expanded (2.0f));
            well (g, link.getBounds().toFloat().expanded (2.0f));
            well (g, count[0].getBounds().getUnion (count[2].getBounds()).toFloat().expanded (2.0f));
            text (g, ed.fonts->monoFont (10.0f, 0.1f), "BANDS", { count[0].getX() - 64.0f, 0.0f, 56.0f, (float) getHeight() }, haze, Justification::centredRight);
            text (g, ed.fonts->displayFont (11.0f, 0.14f), "TRANSIENTS", { 0.0f, 0.0f, (float) getWidth(), (float) getHeight() }, mist.withAlpha (0.7f));
        }

        void resized() override
        {
            delta.setBounds (10, 7, 78, 24);
            link.setBounds (96, 7, 66, 24);
            for (int i = 0; i < 3; ++i)
                count[i].setBounds (getWidth() - 12 - (3 - i) * 32, 7, 30, 24);
        }

        PunchEditor& ed;
        Pad delta, link, count[3];
    };

    //==============================================================================
    /** The bands side by side along the spectrum, their crossovers to drag, power and solo on each. */
    struct BandStrip final : Component, SettableTooltipClient
    {
        explicit BandStrip (PunchEditor& e) : ed (e)
        {
            setTooltip (String::fromUTF8 ("Cliquez une bande pour afficher ses réglages ; glissez les poignées pour déplacer les fréquences de coupure (double-clic : valeur d'origine). ⏻ coupe le traitement de la bande, S l'écoute seule."));
        }

        float handleX (int k) const { return xOf (edges (ed.processor.apvts)[(size_t) k + 1], (float) getWidth()); }
        Point<float> powerAt (float x0, float x1) const { return { (x0 + x1) * 0.5f - 12.0f, 15.0f }; }
        Point<float> soloAt (float x0, float x1) const  { return { (x0 + x1) * 0.5f + 12.0f, 15.0f }; }

        int handleAt (Point<float> p) const
        {
            for (int k = 0; k < bandCount (ed.processor.apvts) - 1; ++k)
                if (std::abs (p.x - handleX (k)) < 8.0f)
                    return k;
            return -1;
        }

        void paint (Graphics& g) override
        {
            auto& s = ed.processor.apvts;
            const auto n = bandCount (s);
            const auto e = edges (s);
            const float W = (float) getWidth(), H = (float) getHeight(), lineY = 36.0f;
            const auto mono = ed.fonts->monoBoldFont (10.0f, 0.08f), small = ed.fonts->monoFont (10.0f);

            for (int b = 0; b < n; ++b)
            {
                const float x0 = xOf (e[(size_t) b], W), x1 = xOf (e[(size_t) b + 1], W);
                const auto colour = bandColour (b);
                const bool chosen = b == ed.selected(), active = isOn (s, PunchProcessor::bandId (b, "on")), solo = isOn (s, PunchProcessor::bandId (b, "solo"));
                const Rectangle<float> zone (x0, 0.0f, x1 - x0, H);

                g.setGradientFill (ColourGradient (colour.withAlpha (chosen ? 0.0f : 0.0f), 0.0f, 0.0f, colour.withAlpha (chosen ? 0.24f : 0.06f), 0.0f, H, false));
                g.fillRect (zone);

                if (chosen)
                {
                    g.setColour (colour.withAlpha (0.9f));
                    g.fillRect (zone.withTop (H - 2.0f));
                }

                Path line;
                line.startNewSubPath (x0 + 1.0f, lineY);
                line.lineTo (x1 - 1.0f, lineY);

                if (active)
                    glowStroke (g, line, colour, 1.6f);
                else
                {
                    Path dashed;
                    const float dashes[] = { 4.0f, 4.0f };
                    PathStrokeType (1.2f).createDashedStroke (dashed, line, dashes, 2);
                    g.setColour (haze.withAlpha (0.5f));
                    g.fillPath (dashed);
                }

                // power
                const auto pw = powerAt (x0, x1);
                g.setColour (Colours::black.withAlpha (0.55f));
                g.fillEllipse (Rectangle<float> (18.0f, 18.0f).withCentre (pw));
                g.setColour (active ? colour : haze.withAlpha (0.6f));
                g.drawEllipse (Rectangle<float> (18.0f, 18.0f).withCentre (pw), 1.2f);
                Path power;
                power.addCentredArc (pw.x, pw.y, 4.0f, 4.0f, 0.0f, degreesToRadians (35.0f), degreesToRadians (325.0f), true);
                power.startNewSubPath (pw.x, pw.y - 5.5f);
                power.lineTo (pw.x, pw.y - 1.0f);
                g.strokePath (power, PathStrokeType (1.4f, PathStrokeType::curved, PathStrokeType::rounded));

                // solo
                const auto so = soloAt (x0, x1);
                const auto circle = Rectangle<float> (18.0f, 18.0f).withCentre (so);
                g.setColour (solo ? colour : Colours::black.withAlpha (0.55f));
                g.fillEllipse (circle);
                g.setColour (solo ? colour : Colours::white.withAlpha (0.22f));
                g.drawEllipse (circle, 1.2f);
                text (g, ed.fonts->monoBoldFont (9.0f), "S", circle, solo ? ink : mist);

                // name and range
                const auto label = Rectangle<float> (x0 + 10.0f, 44.0f, x1 - x0 - 20.0f, 16.0f);
                text (g, mono, bandName (b, n), label, chosen ? bone : mist, Justification::centredLeft);
                if (label.getWidth() > 150.0f)
                    text (g, small, hzText (e[(size_t) b]) + String::fromUTF8 (" – ") + hzText (e[(size_t) b + 1]), label, haze, Justification::centredRight);
            }

            for (int k = 0; k < n - 1; ++k)
            {
                const float x = handleX (k);
                const bool hot = k == dragging || k == hovered;
                g.setColour (Colours::white.withAlpha (hot ? 0.45f : 0.2f));
                g.fillRect (x - 0.5f, 0.0f, 1.0f, H);

                const auto grip = Rectangle<float> (10.0f, 26.0f).withCentre ({ x, lineY });
                g.setColour (Colours::black.withAlpha (0.5f));
                g.fillRoundedRectangle (grip.translated (0.0f, 2.0f), 5.0f);
                g.setGradientFill (ColourGradient (bone, x, grip.getY(), Colour (0xff9d8d92), x, grip.getBottom(), false));
                g.fillRoundedRectangle (grip, 5.0f);
                g.setColour (ink.withAlpha (0.6f));
                for (int i = -1; i <= 1; ++i)
                    g.fillRect (x - 2.5f, lineY + (float) i * 4.0f - 0.5f, 5.0f, 1.0f);

                if (hot)
                {
                    const auto t = hzText (edges (s)[(size_t) k + 1]);
                    const auto f = ed.fonts->monoBoldFont (10.0f);
                    const auto w = GlyphArrangement::getStringWidth (f, t) + 14.0f;
                    auto pill = Rectangle<float> (w, 18.0f).withCentre ({ x, 10.0f });
                    pill = pill.withX (jlimit (2.0f, W - w - 2.0f, pill.getX()));
                    g.setColour (Colour (0xf0120a0c));
                    g.fillRoundedRectangle (pill, 9.0f);
                    g.setColour (look::emberPink.withAlpha (0.8f));
                    g.drawRoundedRectangle (pill.reduced (0.5f), 9.0f, 1.0f);
                    text (g, f, t, pill, bone);
                }
            }
        }

        void mouseMove (const MouseEvent& e) override
        {
            const auto h = handleAt (e.position);
            if (h != hovered) { hovered = h; repaint(); }
            setMouseCursor (h >= 0 ? MouseCursor::LeftRightResizeCursor : MouseCursor::PointingHandCursor);
        }

        void mouseExit (const MouseEvent&) override { hovered = -1; repaint(); }

        void mouseDown (const MouseEvent& e) override
        {
            auto& s = ed.processor.apvts;
            dragging = handleAt (e.position);

            if (dragging >= 0)
            {
                s.getParameter (dragging == 0 ? "xover1" : "xover2")->beginChangeGesture();
                return;
            }

            const auto n = bandCount (s);
            const auto ed2 = edges (s);
            const float W = (float) getWidth();

            for (int b = 0; b < n; ++b)
            {
                const float x0 = xOf (ed2[(size_t) b], W), x1 = xOf (ed2[(size_t) b + 1], W);

                if (powerAt (x0, x1).getDistanceFrom (e.position) < 10.0f) { flip (s, PunchProcessor::bandId (b, "on")); return; }
                if (soloAt (x0, x1).getDistanceFrom (e.position) < 10.0f)  { flip (s, PunchProcessor::bandId (b, "solo")); return; }
                if (e.position.x >= x0 && e.position.x < x1)               { ed.select (b); return; }
            }
        }

        void mouseDrag (const MouseEvent& e) override
        {
            if (dragging < 0)
                return;

            auto& s = ed.processor.apvts;
            const auto n = bandCount (s);
            auto hz = hzOf (e.position.x, (float) getWidth());

            if (dragging == 0)
                hz = jlimit (40.0f, n == 3 ? jmin (4000.0f, value (s, "xover2") / 1.25f) : 4000.0f, hz);
            else
                hz = jlimit (jmax (400.0f, value (s, "xover1") * 1.25f), 16000.0f, hz);

            auto* p = s.getParameter (dragging == 0 ? "xover1" : "xover2");
            p->setValueNotifyingHost (p->convertTo0to1 (hz));
            repaint();
        }

        void mouseUp (const MouseEvent&) override
        {
            if (dragging >= 0)
                ed.processor.apvts.getParameter (dragging == 0 ? "xover1" : "xover2")->endChangeGesture();
            dragging = -1;
            repaint();
        }

        void mouseDoubleClick (const MouseEvent& e) override
        {
            if (const auto h = handleAt (e.position); h >= 0)
            {
                auto* p = ed.processor.apvts.getParameter (h == 0 ? "xover1" : "xover2");
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->getDefaultValue());
                p->endChangeGesture();
            }
        }

        PunchEditor& ed;
        int dragging = -1, hovered = -1;
    };

    //==============================================================================
    /** The selected band's attack, sustain and how it reacts, floating over the display. */
    struct BandPanel final : Component
    {
        static constexpr int kMargin = 18;

        explicit BandPanel (PunchEditor& e) : ed (e)
        {
            for (auto* s : { &attack, &sustain })
            {
                s->setSliderStyle (Slider::LinearVertical);
                s->setTextBoxStyle (Slider::NoTextBox, true, 0, 0);
                s->getProperties().set ("kind", "band");
                s->setVelocityBasedMode (false);
                addAndMakeVisible (s);
            }

            attack.setTooltip (String::fromUTF8 ("Attack : monte (+) ou adoucit (−) le début de chaque coup. Vers le haut, la caisse claque et le kick tape ; vers le bas, les attaques s'arrondissent."));
            sustain.setTooltip (String::fromUTF8 ("Sustain : allonge (+) ou raccourcit (−) ce qui suit chaque coup. Vers le bas, la pièce et la résonance disparaissent ; vers le haut, le son s'épaissit."));

            const char* names[] = { "Sharp", "Medium", "Smooth" };
            const char* tips[] = { "Sharp : ne prend que l'instant du coup, très court et très net.",
                                   "Medium : le début du coup, avec un peu de corps.",
                                   "Smooth : un façonnage plus large et plus doux, qui laisse respirer l'attaque." };

            for (int i = 0; i < 3; ++i)
            {
                modes[i].draw = [this, i, name = String (names[i])] (Graphics& g, Rectangle<float> r, bool over)
                {
                    const bool lit = roundToInt (value (ed.processor.apvts, PunchProcessor::bandId (band, "mode"))) == i;
                    da::seg (g, r, lit ? 1.0f : 0.0f, over ? 1.0f : 0.0f, 1.0f, 7.0f);
                    const auto colour = lit ? bandColour (band) : haze;
                    g.setColour (colour);
                    g.strokePath (modeIcon (i, r.withTrimmedLeft (10.0f).withWidth (18.0f).withSizeKeepingCentre (18.0f, 12.0f)),
                                  PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
                    text (g, ed.fonts->bodyFont (13.0f), name, r.withTrimmedLeft (38.0f), lit ? bone : mist, Justification::centredLeft);
                };
                modes[i].onClick = [this, i] { set (ed.processor.apvts, PunchProcessor::bandId (band, "mode"), (float) i); };
                modes[i].setTooltip (String::fromUTF8 (tips[i]));
                addAndMakeVisible (modes[i]);
            }

            setSize (300 + 2 * kMargin, 146 + 2 * kMargin);
        }

        void bind (int b)
        {
            if (b == band)
                return;

            band = b;
            attachments.clear();

            for (auto [slider, what] : { std::pair (&attack, "attack"), std::pair (&sustain, "sustain") })
            {
                slider->getProperties().set ("colour", (int64) bandColour (b).getARGB());
                attachments.push_back (std::make_unique<State::SliderAttachment> (ed.processor.apvts, PunchProcessor::bandId (b, what), *slider));
                slider->setDoubleClickReturnValue (true, 0.0);
            }

            repaint();
        }

        Rectangle<float> card() const { return getLocalBounds().toFloat().reduced ((float) kMargin); }

        void paint (Graphics& g) override
        {
            auto& s = ed.processor.apvts;
            const auto c = card();
            const auto n = bandCount (s);
            const auto colour = bandColour (band);

            // a darker glass than the panels: it floats over the moving display
            for (int k = 3; k >= 1; --k)
            {
                g.setColour (Colours::black.withAlpha (0.16f / (float) k));
                g.fillRoundedRectangle (c.translated (0.0f, 8.0f).expanded (5.0f * (float) k), 14.0f + 5.0f * (float) k);
            }
            g.setGradientFill (ColourGradient (Colour (0xf41c1114), c.getX(), c.getY(), Colour (0xf4100a0c), c.getX(), c.getBottom(), false));
            g.fillRoundedRectangle (c, 14.0f);
            g.setColour (colour.withAlpha (0.35f));
            g.drawRoundedRectangle (c.reduced (0.5f), 14.0f, 1.0f);
            g.setColour (Colours::white.withAlpha (0.1f));
            g.fillRect (c.getX() + 14.0f, c.getY() + 1.0f, c.getWidth() - 28.0f, 1.0f);

            // header: the band, its range
            const auto e = edges (s);
            g.setColour (colour);
            g.fillEllipse (c.getX() + 14.0f, c.getY() + 14.0f, 7.0f, 7.0f);
            text (g, ed.fonts->displayFont (10.0f, 0.14f), bandName (band, n), { c.getX() + 27.0f, c.getY() + 9.0f, 80.0f, 17.0f }, bone, Justification::centredLeft);
            text (g, ed.fonts->monoFont (10.0f), hzText (e[(size_t) band]) + String::fromUTF8 (" – ") + hzText (e[(size_t) band + 1]),
                  { c.getRight() - 160.0f, c.getY() + 9.0f, 146.0f, 17.0f }, haze, Justification::centredRight);

            g.setColour (Colours::white.withAlpha (0.08f));
            g.fillRect (c.getX() + 140.0f, c.getY() + 36.0f, 1.0f, c.getHeight() - 48.0f);

            for (auto* slider : { &attack, &sustain })
            {
                const auto b = slider->getBounds().toFloat();
                text (g, ed.fonts->monoBoldFont (11.0f), slider->getTextFromValue (slider->getValue()).replace ("-", String::fromUTF8 ("−")),
                      { b.getCentreX() - 34.0f, b.getBottom() + 2.0f, 68.0f, 14.0f }, bone);
                text (g, ed.fonts->monoFont (9.0f, 0.1f), slider == &attack ? "ATTACK" : "SUSTAIN",
                      { b.getCentreX() - 34.0f, b.getBottom() + 16.0f, 68.0f, 12.0f }, haze);
            }
        }

        void resized() override
        {
            const auto c = card().toNearestInt();
            attack.setBounds (c.getX() + 24, c.getY() + 32, 30, 76);
            sustain.setBounds (c.getX() + 86, c.getY() + 32, 30, 76);
            for (int i = 0; i < 3; ++i)
                modes[i].setBounds (c.getX() + 152, c.getY() + 38 + i * 32, c.getWidth() - 166, 28);
        }

        PunchEditor& ed;
        int band = -1;
        Slider attack, sustain;
        Pad modes[3];
        std::vector<std::unique_ptr<State::SliderAttachment>> attachments;
    };

    //==============================================================================
    /** The gain the selected band applies, scrolling, over its level; its controls float over it. */
    struct Graph final : Component
    {
        explicit Graph (PunchEditor& e) : ed (e), panel (e) { addAndMakeVisible (panel); }

        static constexpr float kLeft = 42.0f, kTop = 16.0f, kBottom = 12.0f, kRight = 10.0f;

        Rectangle<float> plot() const
        {
            return getLocalBounds().toFloat().withTrimmedLeft (kLeft).withTrimmedTop (kTop).withTrimmedBottom (kBottom).withTrimmedRight (kRight);
        }

        float yOfDb (float db) const
        {
            static const float marks[][2] = { { 12, 0.0f }, { 9, 0.08f }, { 6, 0.16f }, { 3, 0.24f }, { 0, 0.32f }, { -3, 0.40f },
                                              { -6, 0.48f }, { -12, 0.62f }, { -24, 0.80f }, { -48, 1.0f } };
            const auto p = plot();
            float t = 1.0f;

            if (db >= 12.0f) t = 0.0f;
            else
                for (int i = 1; i < 10; ++i)
                    if (db >= marks[i][0])
                    {
                        t = marks[i - 1][1] + (marks[i][1] - marks[i - 1][1]) * (marks[i - 1][0] - db) / (marks[i - 1][0] - marks[i][0]);
                        break;
                    }

            return p.getY() + t * p.getHeight();
        }

        void place()
        {
            auto& s = ed.processor.apvts;
            const auto b = ed.selected();
            const auto e = edges (s);
            const float W = (float) getWidth();
            const float centre = (xOf (e[(size_t) b], W) + xOf (e[(size_t) b + 1], W)) * 0.5f;
            panel.bind (b);
            const auto m = (float) BandPanel::kMargin;
            const auto x = jlimit (kLeft + 8.0f - m, W - (float) panel.getWidth() + m - 12.0f, centre - (float) panel.getWidth() * 0.5f);
            panel.setTopLeftPosition (roundToInt (x), 10);
        }

        void resized() override { place(); }

        void paint (Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            const auto p = plot();

            // the display: dark glass, rounded at the bottom like the card it sits in
            Path inner;
            inner.addRoundedRectangle (r.getX() + 1, r.getY(), r.getWidth() - 2, r.getHeight() - 1, 13.0f, 13.0f, false, false, true, true);
            g.setGradientFill (ColourGradient (Colour (0xe00d0709), 0.0f, 0.0f, Colour (0xf0060404), 0.0f, r.getBottom(), false));
            g.fillPath (inner);

            const auto mono = ed.fonts->monoFont (10.0f);
            for (float db : { 12.0f, 9.0f, 6.0f, 3.0f, 0.0f, -3.0f, -6.0f, -12.0f, -24.0f, -48.0f })
            {
                const auto y = yOfDb (db);
                g.setColour (Colours::white.withAlpha (db == 0.0f ? 0.13f : 0.045f));
                g.fillRect (p.getX(), y - 0.5f, p.getWidth(), 1.0f);
                text (g, mono, db == -48.0f ? String::fromUTF8 ("−inf") : dbText (db, 0).replace ("+", ""), { 4.0f, y - 7.0f, kLeft - 12.0f, 14.0f }, haze, Justification::centredRight);
            }

            for (float x = p.getRight() - 100.0f; x > p.getX(); x -= 100.0f)
            {
                g.setColour (Colours::white.withAlpha (0.025f));
                g.fillRect (x, p.getY(), 1.0f, p.getHeight());
            }

            auto& shaper = ed.processor.shaper;
            const int written = shaper.written.load (std::memory_order_acquire);
            const int count = jmin (written, punch::kHistory - 1, (int) p.getWidth());
            const int band = ed.selected();
            const auto colour = bandColour (band);

            if (count < 2)
                return;

            Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (p.toNearestInt());

            const auto& history = shaper.history[(size_t) band];
            auto at = [&] (int i) -> const punch::Shaper::Point& { return history[(size_t) ((written - 1 - i) % punch::kHistory)]; };
            const float floorTop = p.getBottom() - p.getHeight() * 0.3f;

            // the band's level, rising from the floor
            Path wave, edge;
            wave.startNewSubPath (p.getRight(), p.getBottom());
            for (int i = 0; i < count; ++i)
            {
                const auto db = 20.0f * std::log10 (jmax (at (i).level.load (std::memory_order_relaxed), 1.0e-6f));
                const auto y = jmap (jlimit (-60.0f, 0.0f, db), -60.0f, 0.0f, p.getBottom(), floorTop);
                wave.lineTo (p.getRight() - (float) i, y);
                if (i == 0) edge.startNewSubPath (p.getRight(), y); else edge.lineTo (p.getRight() - (float) i, y);
            }
            wave.lineTo (p.getRight() - (float) (count - 1), p.getBottom());
            wave.closeSubPath();

            g.setGradientFill (ColourGradient (colour.withAlpha (0.30f), 0.0f, floorTop, colour.withAlpha (0.03f), 0.0f, p.getBottom(), false));
            g.fillPath (wave);
            g.setColour (colour.withAlpha (0.35f));
            g.strokePath (edge, PathStrokeType (1.0f));

            // the gain applied, with the area between it and 0 dB lightly filled
            const auto zero = yOfDb (0.0f);
            Path gain, area;
            float headY = zero;
            area.startNewSubPath (p.getRight(), zero);
            for (int i = 0; i < count; ++i)
            {
                const auto y = yOfDb (at (i).gainDb.load (std::memory_order_relaxed));
                if (i == 0) { gain.startNewSubPath (p.getRight(), y); headY = y; } else gain.lineTo (p.getRight() - (float) i, y);
                area.lineTo (p.getRight() - (float) i, y);
            }
            area.lineTo (p.getRight() - (float) (count - 1), zero);
            area.closeSubPath();

            ColourGradient fill (colour.withAlpha (0.28f), 0.0f, yOfDb (12.0f), colour.withAlpha (0.28f), 0.0f, yOfDb (-24.0f), false);
            fill.addColour (jlimit (0.01, 0.99, (double) ((zero - yOfDb (12.0f)) / (yOfDb (-24.0f) - yOfDb (12.0f)))), colour.withAlpha (0.0f));
            g.setGradientFill (fill);
            g.fillPath (area);
            glowStroke (g, gain, colour, 1.8f);

            const Point<float> head { p.getRight() - 1.0f, headY };
            g.setColour (colour.withAlpha (0.25f));
            g.fillEllipse (Rectangle<float> (16.0f, 16.0f).withCentre (head));
            g.setColour (bone);
            g.fillEllipse (Rectangle<float> (6.0f, 6.0f).withCentre (head));
        }

        PunchEditor& ed;
        BandPanel panel;
    };

    //==============================================================================
    /** Input and output faders with their meters, then bypass, the stereo tools and the blend. */
    struct Side final : Component
    {
        static constexpr float kBarsTop = 50, kBarsH = 240;

        explicit Side (PunchEditor& e) : ed (e)
        {
            auto& s = ed.processor.apvts;

            auto slider = [&] (Slider& sl, const char* id, Slider::SliderStyle style, const char* kind, const char* tip)
            {
                sl.setSliderStyle (style);
                sl.setTextBoxStyle (Slider::NoTextBox, true, 0, 0);
                sl.getProperties().set ("kind", kind);
                sl.setTooltip (String::fromUTF8 (tip));
                attachments.push_back (std::make_unique<State::SliderAttachment> (s, id, sl));
                auto* p = s.getParameter (id);
                sl.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
                addAndMakeVisible (sl);
            };

            slider (input, "ingain", Slider::LinearVertical, "fader", "Input : le niveau qui entre dans Punch, de −24 à +12 dB.");
            slider (output, "outgain", Slider::LinearVertical, "fader", "Output : le niveau qui en sort, de −24 à +12 dB.");
            slider (pan, "pan", Slider::LinearBarVertical, "box", "Pan : la balance gauche / droite. Glissez vers le haut ou le bas ; double-clic : au centre.");
            slider (width, "width", Slider::LinearBarVertical, "box", "Width : l'image stéréo, de −100 (mono) à +100 (deux fois plus large).");
            slider (mix, "mix", Slider::LinearHorizontal, "mix", "Mix : le son façonné mêlé au son d'origine, de 0 à 100 %.");

            const auto mono = ed.fonts->monoBoldFont (11.0f, 0.08f);

            bypass.draw = [&s, mono] (Graphics& g, Rectangle<float> r, bool over)
            {
                well (g, r, r.getHeight() * 0.5f);
                da::seg (g, r.reduced (3.0f), isOn (s, "bypass") ? 1.0f : 0.0f, over ? 1.0f : 0.0f, 1.0f, (r.getHeight() - 6.0f) * 0.5f);
                text (g, mono, "BYPASS", r, isOn (s, "bypass") ? bone : mist);
            };
            bypass.onClick = [&s] { flip (s, "bypass"); };
            bypass.setTooltip (String::fromUTF8 ("Bypass : le son passe tel quel, pour comparer."));

            struct Tool { Pad* pad; const char* id; const char* tip; };
            for (auto [pad, id, tip] : { Tool { &monoPad, "mono", "Mono : additionne la gauche et la droite." },
                                         Tool { &phase, "phase", "Phase : inverse la polarité du signal." },
                                         Tool { &swap, "swap", "Swap : échange la gauche et la droite." } })
            {
                const String paramId (id);
                pad->draw = [&s, paramId, this, pad] (Graphics& g, Rectangle<float> r, bool over)
                {
                    const bool lit = isOn (s, paramId);
                    da::seg (g, r, lit ? 1.0f : 0.0f, over ? 1.0f : 0.0f, 1.0f, 7.0f);
                    const auto c = r.getCentre();
                    g.setColour (lit ? bone : mist);

                    if (pad == &monoPad)
                    {
                        g.drawEllipse (Rectangle<float> (11.0f, 11.0f).withCentre (c.translated (-3.5f, 0.0f)), 1.3f);
                        g.drawEllipse (Rectangle<float> (11.0f, 11.0f).withCentre (c.translated (3.5f, 0.0f)), 1.3f);
                    }
                    else if (pad == &phase)
                    {
                        g.drawEllipse (Rectangle<float> (11.0f, 11.0f).withCentre (c), 1.3f);
                        g.drawLine (c.x - 6.0f, c.y + 6.0f, c.x + 6.0f, c.y - 6.0f, 1.3f);
                    }
                    else
                    {
                        Path arrows;
                        arrows.startNewSubPath (c.x - 8.0f, c.y); arrows.lineTo (c.x + 8.0f, c.y);
                        arrows.startNewSubPath (c.x - 4.0f, c.y - 4.0f); arrows.lineTo (c.x - 8.0f, c.y); arrows.lineTo (c.x - 4.0f, c.y + 4.0f);
                        arrows.startNewSubPath (c.x + 4.0f, c.y - 4.0f); arrows.lineTo (c.x + 8.0f, c.y); arrows.lineTo (c.x + 4.0f, c.y + 4.0f);
                        g.strokePath (arrows, PathStrokeType (1.3f, PathStrokeType::curved, PathStrokeType::rounded));
                    }
                };
                pad->onClick = [&s, paramId] { flip (s, paramId); };
                pad->setTooltip (String::fromUTF8 (tip));
                addAndMakeVisible (pad);
            }

            addAndMakeVisible (bypass);
        }

        static float meterY (float db)
        {
            static const float marks[][2] = { { 0, 0.0f }, { -3, 0.08f }, { -6, 0.16f }, { -10, 0.26f }, { -15, 0.37f }, { -20, 0.47f },
                                              { -30, 0.63f }, { -40, 0.76f }, { -50, 0.88f }, { -60, 1.0f } };
            float t = 1.0f;
            if (db >= 0.0f) t = 0.0f;
            else
                for (int i = 1; i < 10; ++i)
                    if (db >= marks[i][0])
                    {
                        t = marks[i - 1][1] + (marks[i][1] - marks[i - 1][1]) * (marks[i - 1][0] - db) / (marks[i - 1][0] - marks[i][0]);
                        break;
                    }
            return kBarsTop + t * kBarsH;
        }

        static constexpr float barX[4] = { 42, 57, 136, 151 };

        Rectangle<int> meterArea() const { return { 36, 30, 136, 280 }; }

        void paint (Graphics& g) override
        {
            const auto label = ed.fonts->monoFont (10.0f, 0.12f), small = ed.fonts->monoFont (9.0f), value = ed.fonts->monoBoldFont (12.0f);

            text (g, label, "IN", { 8.0f, 12.0f, 64.0f, 14.0f }, haze);
            text (g, label, "OUT", { 136.0f, 12.0f, 64.0f, 14.0f }, haze);

            for (int k = 0; k < 4; ++k)
            {
                const auto bar = Rectangle<float> (barX[k], kBarsTop, 12.0f, kBarsH);
                g.setColour (Colours::black.withAlpha (0.55f));
                g.fillRoundedRectangle (bar.expanded (1.0f), 2.5f);

                const auto top = meterY (ed.level[k]);
                if (top < bar.getBottom())
                {
                    ColourGradient lit (bone, 0.0f, kBarsTop, look::emberDeep, 0.0f, kBarsTop + kBarsH, false);
                    lit.addColour (0.12, look::emberPink);
                    lit.addColour (0.45, look::ember);
                    g.setGradientFill (lit);
                    g.fillRect (bar.withTop (top));
                }

                // the bar in small cells, like a row of lamps
                g.setColour (Colour (0xff0a0607).withAlpha (0.55f));
                for (float y = kBarsTop + 3.0f; y < kBarsTop + kBarsH; y += 3.0f)
                    g.fillRect (bar.getX(), y, bar.getWidth(), 1.0f);

                if (ed.held[k] > -60.0f)
                {
                    g.setColour (ed.held[k] > -0.1f ? look::emberLit : bone.withAlpha (0.85f));
                    g.fillRect (bar.getX(), meterY (ed.held[k]) - 1.0f, bar.getWidth(), 2.0f);
                }
            }

            text (g, small, dbText (jmax (ed.held[0], ed.held[1])), { 36.0f, 30.0f, 40.0f, 14.0f }, mist);
            text (g, small, dbText (jmax (ed.held[2], ed.held[3])), { 130.0f, 30.0f, 40.0f, 14.0f }, mist);

            for (float db : { 0.0f, -3.0f, -6.0f, -10.0f, -15.0f, -20.0f, -30.0f, -40.0f, -50.0f, -60.0f })
                text (g, small, db == -60.0f ? String::fromUTF8 ("−inf") : dbText (db, 0), { 72.0f, meterY (db) - 6.0f, 64.0f, 12.0f }, haze);

            for (auto x : { 42.0f, 136.0f })
            {
                text (g, small, "L", { x, kBarsTop + kBarsH + 4.0f, 12.0f, 12.0f }, haze);
                text (g, small, "R", { x + 15.0f, kBarsTop + kBarsH + 4.0f, 12.0f, 12.0f }, haze);
            }

            text (g, value, dbText ((float) input.getValue()), { 6.0f, 314.0f, 70.0f, 16.0f }, bone);
            text (g, value, dbText ((float) output.getValue()), { 132.0f, 314.0f, 70.0f, 16.0f }, bone);

            well (g, Rectangle<float> (10.0f, 384.0f, 188.0f, 32.0f));
            text (g, label, "PAN", { 10.0f, 428.0f, 90.0f, 14.0f }, haze);
            text (g, label, "WIDTH", { 108.0f, 428.0f, 90.0f, 14.0f }, haze);
            text (g, label, "MIX", { 12.0f, 498.0f, 60.0f, 14.0f }, haze, Justification::centredLeft);
            text (g, value, mix.getTextFromValue (mix.getValue()), { 120.0f, 497.0f, 76.0f, 16.0f }, bone, Justification::centredRight);
        }

        void resized() override
        {
            input.setBounds (8, (int) kBarsTop - 8, 30, (int) kBarsH + 16);
            output.setBounds (170, (int) kBarsTop - 8, 30, (int) kBarsH + 16);
            bypass.setBounds (10, 340, 188, 34);
            monoPad.setBounds (12, 386, 61, 28);
            phase.setBounds (74, 386, 60, 28);
            swap.setBounds (135, 386, 61, 28);
            pan.setBounds (10, 444, 90, 30);
            width.setBounds (108, 444, 90, 30);
            mix.setBounds (10, 514, 188, 26);
        }

        PunchEditor& ed;
        Slider input, output, pan, width, mix;
        Pad bypass, monoPad, phase, swap;
        std::vector<std::unique_ptr<State::SliderAttachment>> attachments;
    };

    Image sigil()
    {
        return ImageCache::getFromMemory (BinaryData::sigil_png, BinaryData::sigil_pngSize);
    }
}

//==============================================================================
PunchEditor::PunchEditor (PunchProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      look (std::make_unique<Look>()),
      bar (p.presets, p.apvts, "PUNCH", sigil(), kUnits, kUnits)
{
    setLookAndFeel (look.get());

    front.onPaint = [] (Graphics& g)
    {
        da::glass (g, kLeftCard, 14.0f, 1.0f);
        da::glass (g, kSideCard, 14.0f, 1.0f);

        g.setColour (Colours::white.withAlpha (0.07f));
        g.fillRect (kLeftCard.getX() + 1.0f, kLeftCard.getY() + kModuleH, kLeftCard.getWidth() - 2.0f, 1.0f);
        g.fillRect (kLeftCard.getX() + 1.0f, kLeftCard.getY() + kModuleH + kStripH, kLeftCard.getWidth() - 2.0f, 1.0f);
    };

    module = std::make_unique<ModuleRow> (*this);
    strip = std::make_unique<BandStrip> (*this);
    graph = std::make_unique<Graph> (*this);
    side = std::make_unique<Side> (*this);

    const auto l = kLeftCard.toNearestInt();
    module->setBounds (l.getX(), l.getY(), l.getWidth(), (int) kModuleH);
    strip->setBounds (l.getX(), l.getY() + (int) kModuleH, l.getWidth(), (int) kStripH);
    graph->setBounds (l.getX(), l.getY() + (int) (kModuleH + kStripH), l.getWidth(), l.getHeight() - (int) (kModuleH + kStripH));
    side->setBounds (kSideCard.toNearestInt());

    for (auto* c : { module.get(), strip.get(), graph.get(), side.get() })
        front.addAndMakeVisible (c);

    addAndMakeVisible (front);
    addAndMakeVisible (bar);
    bar.onThemeChanged = [this] { themeSeen = -1; };

    licenseOverlay = std::make_unique<license::Overlay> ("PUNCH", processor.license);
    addAndMakeVisible (*licenseOverlay);

    constexpr int defaultWidth = kUnits, minWidth = defaultWidth * 3 / 4, maxWidth = defaultWidth * 2;
    setResizable (true, true);
    setResizeLimits (minWidth, minWidth * kTotalHeight / kUnits, maxWidth, maxWidth * kTotalHeight / kUnits);
    getConstrainer()->setFixedAspectRatio ((double) kUnits / (double) kTotalHeight);

    const auto opening = window::openingSize ({}, "ONIRIQUE PUNCH", { defaultWidth, defaultWidth * kTotalHeight / kUnits },
                                              { minWidth, minWidth * kTotalHeight / kUnits }, { maxWidth, maxWidth * kTotalHeight / kUnits });
    const auto width = jmin (opening.x, opening.y * kUnits / kTotalHeight);
    setSize (width, width * kTotalHeight / kUnits);

    timerCallback();
    startTimerHz (60);
}

PunchEditor::~PunchEditor()
{
    window::rememberOnThisComputer ("ONIRIQUE PUNCH", getWidth(), getHeight());
    setLookAndFeel (nullptr);
}

int PunchEditor::selected() const
{
    return jlimit (0, bandCount (processor.apvts) - 1, (int) processor.apvts.state.getProperty ("selected", 1));
}

void PunchEditor::select (int band)
{
    processor.apvts.state.setProperty ("selected", band, nullptr);
}

void PunchEditor::resized()
{
    const auto scale = (float) getWidth() / (float) kUnits;

    front.setBounds (0, kBarHeight, kUnits, kFrontHeight);
    front.setTransform (AffineTransform::scale (scale));

    bar.setPillWidth (licenseOverlay != nullptr ? licenseOverlay->pillWidth() : 0);
    bar.place (scale, getLocalBounds());

    if (licenseOverlay != nullptr)
        licenseOverlay->fit (getLocalBounds(), scale, bar.getPillArea(), Justification::centredRight);
}

void PunchEditor::paint (Graphics& g)
{
    if (backdrop.getWidth() != getWidth() || backdrop.getHeight() != getHeight())
        backdrop = da::backdrop (getWidth(), getHeight(), (float) getWidth() / (float) kUnits);
    g.drawImageAt (backdrop, 0, 0);
}

void PunchEditor::timerCallback()
{
    if (look::changedSince (themeSeen))
    {
        backdrop = {};
        repaint();
    }

    // The meters: up at once, down at about 40 dB a second; the peak held for a second and a half.
    const bool monoBus = processor.getTotalNumOutputChannels() < 2;
    for (int k = 0; k < 4; ++k)
    {
        const auto source = monoBus ? (k & 2) : k;
        const auto db = 20.0f * std::log10 (jmax (processor.shaper.takePeak (source >= 2, source & 1), 1.0e-6f));
        level[k] = db > level[k] ? db : jmax (-100.0f, level[k] - 0.7f);

        if (db >= held[k]) { held[k] = db; heldFor[k] = 90; }
        else if (--heldFor[k] < 0) held[k] = jmax (-100.0f, held[k] - 0.4f);
    }

    if (monoBus) { level[1] = level[0]; level[3] = level[2]; held[1] = held[0]; held[3] = held[2]; }

    side->repaint (static_cast<Side*> (side.get())->meterArea());
    graph->repaint();

    // Anything set from elsewhere (the host, a preset, the other controls) is drawn again.
    std::vector<float> values;
    for (auto* parameter : processor.getParameters())
        values.push_back (parameter->getValue());

    if (values != seenValues || selected() != seenSelected)
    {
        seenValues = std::move (values);
        seenSelected = selected();
        static_cast<Graph*> (graph.get())->place();
        static_cast<Graph*> (graph.get())->panel.repaint();
        module->repaint();
        strip->repaint();
        side->repaint();
    }
}

} // namespace onirique
