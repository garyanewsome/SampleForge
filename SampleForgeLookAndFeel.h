#pragma once

#include <JuceHeader.h>

//==============================================================================
// Black stage, white light. Every control is drawn in one cool white, with
// soft glows on text, lit fader tracks and the waveform; the only other
// colour is a warm red reserved for "something is wrong" (missing samples).
//==============================================================================
class SampleForgeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static juce::Colour getWindowBackgroundColour() { return juce::Colour(0xff030304); }
    static juce::Colour getPanelColour()            { return juce::Colour(0xff09090b); }
    static juce::Colour getLineColour()             { return juce::Colour(0xff2c2e34); }
    static juce::Colour getAccentColour()           { return juce::Colour(0xfff2f5ff); } // glowing white
    static juce::Colour getTextColour()             { return juce::Colour(0xfff2f5ff); }
    static juce::Colour getDimTextColour()          { return juce::Colour(0xff8b90a0); }
    static juce::Colour getWarningColour()          { return juce::Colour(0xffff7a7a); }

    static juce::Font uiFont(float height, bool bold = false)
    {
        return juce::Font(juce::FontOptions("Helvetica Neue", height, bold ? juce::Font::bold : juce::Font::plain));
    }
    static juce::Font monoFont(float height, bool bold = false)
    {
        return juce::Font(juce::FontOptions("Menlo", height, bold ? juce::Font::bold : juce::Font::plain));
    }

    // Text with a soft halo: a few faint copies pushed out in each direction
    // under the crisp text. Cheap, and reads as light bleeding off the glyphs.
    static void drawGlowText(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                             juce::Justification justification, const juce::Font& font,
                             juce::Colour colour = getTextColour(), float glow = 1.0f)
    {
        g.setFont(font);
        if (glow > 0.0f)
        {
            struct Ring { int d; float alpha; };
            for (auto ring : { Ring { 2, 0.045f }, Ring { 1, 0.10f } })
            {
                g.setColour(colour.withAlpha(ring.alpha * glow));
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dy = -1; dy <= 1; ++dy)
                        if (dx != 0 || dy != 0)
                            g.drawText(text, area.translated(dx * ring.d, dy * ring.d), justification, true);
            }
        }
        g.setColour(colour);
        g.drawText(text, area, justification, true);
    }

    SampleForgeLookAndFeel()
    {
        const auto white = getTextColour();
        const auto bg = getWindowBackgroundColour();
        const auto panel = getPanelColour();

        setColour(juce::ResizableWindow::backgroundColourId, bg);

        setColour(juce::Label::textColourId, white);
        setColour(juce::Slider::textBoxTextColourId, white);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::black);
        setColour(juce::Slider::textBoxOutlineColourId, getLineColour());
        setColour(juce::Slider::textBoxHighlightColourId, white.withAlpha(0.25f));
        setColour(juce::Slider::thumbColourId, white);
        setColour(juce::Slider::trackColourId, white);
        setColour(juce::Slider::backgroundColourId, getLineColour());

        setColour(juce::TextButton::buttonColourId, juce::Colours::black);
        setColour(juce::TextButton::buttonOnColourId, white.withAlpha(0.2f));
        setColour(juce::TextButton::textColourOffId, white);
        setColour(juce::TextButton::textColourOnId, white);
        setColour(juce::ToggleButton::textColourId, white);
        setColour(juce::ToggleButton::tickColourId, white);
        setColour(juce::ToggleButton::tickDisabledColourId, getLineColour());

        setColour(juce::ComboBox::backgroundColourId, juce::Colours::black);
        setColour(juce::ComboBox::outlineColourId, getLineColour());
        setColour(juce::ComboBox::textColourId, white);
        setColour(juce::ComboBox::arrowColourId, white);
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff060608));
        setColour(juce::PopupMenu::textColourId, white);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, white.withAlpha(0.14f));
        setColour(juce::PopupMenu::highlightedTextColourId, white);

        setColour(juce::ListBox::backgroundColourId, juce::Colours::black);
        setColour(juce::ListBox::outlineColourId, getLineColour());
        setColour(juce::ListBox::textColourId, white);

        // On-screen keyboard: dark, with the keys lighting up white when played.
        setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xff15161a));
        setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour(0xff000000));
        setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, getLineColour());
        setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, white.withAlpha(0.10f));
        setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, white.withAlpha(0.75f));
        setColour(juce::MidiKeyboardComponent::textLabelColourId, getDimTextColour());
        setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);

        juce::ignoreUnused(panel);
    }

    //==========================================================================
    juce::Font getLabelFont(juce::Label& l) override
    {
        const bool isSliderText = dynamic_cast<juce::Slider*>(l.getParentComponent()) != nullptr;
        return isSliderText ? monoFont(11.0f) : uiFont(13.0f);
    }

    void drawLabel(juce::Graphics& g, juce::Label& label) override
    {
        g.fillAll(label.findColour(juce::Label::backgroundColourId));

        if (label.isBeingEdited())
            return;

        const auto font = getLabelFont(label);
        const auto area = getLabelBorderSize(label).subtractedFrom(label.getLocalBounds());
        const bool isSliderText = dynamic_cast<juce::Slider*>(label.getParentComponent()) != nullptr;
        const float alpha = label.isEnabled() ? 1.0f : 0.4f;

        drawGlowText(g, label.getText(), area, label.getJustificationType(), font,
                     label.findColour(juce::Label::textColourId).withMultipliedAlpha(alpha),
                     isSliderText ? 0.0f : 0.35f);

        if (label.getParentComponent() != nullptr && dynamic_cast<juce::Slider*>(label.getParentComponent()) != nullptr)
        {
            g.setColour(label.findColour(juce::Label::outlineColourId));
            g.drawRect(label.getLocalBounds());
        }
    }

    juce::Font getTextButtonFont(juce::TextButton&, int) override { return uiFont(13.0f, true); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return uiFont(13.0f); }
    juce::Font getPopupMenuFont() override { return uiFont(13.0f); }

    //==========================================================================
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float, float,
                          juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        const bool active = slider.isMouseOverOrDragging();
        const float enabledAlpha = slider.isEnabled() ? 1.0f : 0.35f;
        const auto white = getTextColour().withMultipliedAlpha(enabledAlpha);

        if (style == juce::Slider::LinearVertical)
        {
            drawVerticalFader(g, x, y, width, height, sliderPos, slider, active, enabledAlpha);
            return;
        }

        if (style != juce::Slider::LinearHorizontal)
        {
            LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, 0, 0, style, slider);
            return;
        }

        // Thin track, lit up to the thumb, round haloed thumb.
        const float cy = (float) y + (float) height * 0.5f;
        const float left = (float) x + 7.0f, right = (float) x + (float) width - 7.0f;
        const float pos = juce::jlimit(left, right, sliderPos);

        g.setColour(getLineColour().withMultipliedAlpha(enabledAlpha));
        g.fillRoundedRectangle(left, cy - 1.0f, right - left, 2.0f, 1.0f);

        for (float spread : { 6.0f, 3.0f })
        {
            g.setColour(white.withAlpha((active ? 0.16f : 0.09f) * enabledAlpha));
            g.fillRoundedRectangle(left, cy - spread * 0.5f, pos - left, spread, spread * 0.5f);
        }
        g.setColour(white);
        g.fillRoundedRectangle(left, cy - 1.0f, pos - left, 2.0f, 1.0f);

        const float r = 6.0f;
        g.setColour(white.withAlpha((active ? 0.28f : 0.16f) * enabledAlpha));
        g.fillEllipse(pos - r - 3.0f, cy - r - 3.0f, (r + 3.0f) * 2.0f, (r + 3.0f) * 2.0f);
        g.setColour(juce::Colours::black);
        g.fillEllipse(pos - r, cy - r, r * 2.0f, r * 2.0f);
        g.setColour(white);
        g.drawEllipse(pos - r, cy - r, r * 2.0f, r * 2.0f, 1.4f);
        g.fillEllipse(pos - 1.5f, cy - 1.5f, 3.0f, 3.0f);
    }

    void drawVerticalFader(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           juce::Slider& slider, bool active, float enabledAlpha)
    {
        const auto white = getTextColour().withMultipliedAlpha(enabledAlpha);
        const float cx = (float) x + (float) width * 0.5f;
        const float capH = 18.0f, capW = juce::jmin(38.0f, (float) width - 6.0f);

        const float top = (float) y + capH * 0.5f;
        const float bottom = (float) y + (float) height - capH * 0.5f;
        const float pos = juce::jlimit(top, bottom, sliderPos);

        // Ticks: one per detent on stepped controls, otherwise ten divisions.
        int divisions = 10;
        const double interval = slider.getInterval();
        if (interval > 0.0)
        {
            const int steps = juce::roundToInt((slider.getMaximum() - slider.getMinimum()) / interval);
            if (steps >= 1 && steps <= 12)
                divisions = steps;
        }
        const bool stepped = divisions != 10;
        for (int i = 0; i <= divisions; ++i)
        {
            const float ty = bottom - (bottom - top) * (float) i / (float) divisions;
            const bool major = stepped || (i % 5 == 0);
            const float len = major ? 9.0f : 5.0f;
            g.setColour(getDimTextColour().withAlpha(major ? 0.7f : 0.35f));
            g.fillRect(cx - capW * 0.5f - 4.0f - len, ty - 0.5f, len, 1.0f);
            g.fillRect(cx + capW * 0.5f + 4.0f,       ty - 0.5f, len, 1.0f);
        }

        const juce::Rectangle<float> slot(cx - 3.0f, top - 4.0f, 6.0f, bottom - top + 8.0f);
        g.setColour(juce::Colours::black);
        g.fillRoundedRectangle(slot, 3.0f);
        g.setColour(getLineColour());
        g.drawRoundedRectangle(slot, 3.0f, 1.0f);

        const juce::Rectangle<float> fill(cx - 1.5f, pos, 3.0f, bottom - pos + 2.0f);
        for (float spread : { 8.0f, 4.0f })
        {
            g.setColour(white.withAlpha(active ? 0.16f : 0.09f));
            g.fillRoundedRectangle(fill.expanded(spread * 0.5f, 0.0f), 3.0f);
        }
        g.setColour(white);
        g.fillRoundedRectangle(fill, 1.5f);

        // Blade cap: black chamfered body, white edge with a halo, lit centre line.
        const juce::Rectangle<float> cap(cx - capW * 0.5f, pos - capH * 0.5f, capW, capH);
        const float ch = 4.0f;
        juce::Path body;
        body.startNewSubPath(cap.getX() + ch, cap.getY());
        body.lineTo(cap.getRight() - ch, cap.getY());
        body.lineTo(cap.getRight(), cap.getY() + ch);
        body.lineTo(cap.getRight(), cap.getBottom() - ch);
        body.lineTo(cap.getRight() - ch, cap.getBottom());
        body.lineTo(cap.getX() + ch, cap.getBottom());
        body.lineTo(cap.getX(), cap.getBottom() - ch);
        body.lineTo(cap.getX(), cap.getY() + ch);
        body.closeSubPath();

        g.setColour(white.withAlpha(active ? 0.28f : 0.16f));
        g.strokePath(body, juce::PathStrokeType(6.0f));
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff25262c), cap.getX(), cap.getY(),
                                               juce::Colour(0xff050506), cap.getX(), cap.getBottom(), false));
        g.fillPath(body);
        g.setColour(white);
        g.strokePath(body, juce::PathStrokeType(1.2f));

        g.setColour(juce::Colours::black.withAlpha(0.6f));
        for (int i = 1; i <= 3; ++i)
        {
            const float rx = capW * 0.5f - 4.0f - (float) i * 4.0f;
            g.fillRect(cx - rx - 1.0f, cap.getY() + 3.0f, 1.0f, capH - 6.0f);
            g.fillRect(cx + rx,        cap.getY() + 3.0f, 1.0f, capH - 6.0f);
        }
        g.setColour(white);
        g.fillRect(cx - 0.75f, cap.getY() + 2.0f, 1.5f, capH - 4.0f);
    }

    //==========================================================================
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&,
                              bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced(1.0f);
        const auto white = getTextColour();
        g.setColour(juce::Colours::black);
        g.fillRoundedRectangle(r, 3.0f);
        if (highlighted || down)
        {
            g.setColour(white.withAlpha(down ? 0.22f : 0.10f));
            g.fillRoundedRectangle(r, 3.0f);
            g.setColour(white.withAlpha(0.18f));
            g.drawRoundedRectangle(r.expanded(1.5f), 4.0f, 2.5f);
        }
        g.setColour(down || highlighted ? white : white.withAlpha(0.7f));
        g.drawRoundedRectangle(r, 3.0f, 1.2f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        drawGlowText(g, b.getButtonText(), b.getLocalBounds().reduced(4, 0), juce::Justification::centred,
                     getTextButtonFont(b, b.getHeight()), getTextColour().withMultipliedAlpha(b.isEnabled() ? 1.0f : 0.4f), 0.3f);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool) override
    {
        const float s = juce::jmin(18.0f, (float) b.getHeight() - 6.0f);
        const juce::Rectangle<float> box(4.0f, ((float) b.getHeight() - s) * 0.5f, s, s);
        const auto white = getTextColour();

        g.setColour(juce::Colours::black);
        g.fillRoundedRectangle(box, 3.0f);
        if (b.getToggleState())
        {
            g.setColour(white.withAlpha(0.2f));
            g.drawRoundedRectangle(box.expanded(1.5f), 4.0f, 2.5f);
            g.setColour(white);
            g.fillRoundedRectangle(box.reduced(4.0f), 1.5f);
        }
        g.setColour(highlighted || b.getToggleState() ? white : white.withAlpha(0.6f));
        g.drawRoundedRectangle(box, 3.0f, 1.2f);

        drawGlowText(g, b.getButtonText(), b.getLocalBounds().withTrimmedLeft((int) s + 12),
                     juce::Justification::centredLeft, uiFont(13.0f), white, 0.3f);
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int,
                      juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float>(0, 0, (float) width, (float) height).reduced(0.5f);
        g.setColour(juce::Colours::black);
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(box.isMouseOver() ? getTextColour() : getTextColour().withAlpha(0.55f));
        g.drawRoundedRectangle(r, 3.0f, 1.1f);

        juce::Path arrow;
        const float cx = (float) width - 14.0f, cy = (float) height * 0.5f;
        arrow.addTriangle(cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour(getTextColour());
        g.fillPath(arrow);
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(8, 1, box.getWidth() - 28, box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
    }
};
